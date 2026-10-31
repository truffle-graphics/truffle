# ADR 0013: Truffle GUI Architecture

## Status

Accepted as direction (#217). Implementation proceeds through focused
sub-issues of #217 (#221-#235), starting with the element tree (#221).

## Context

The minimum Renderer (#206) and GUI-readiness primitives (#213, #215) are in
place: clip rectangles, sampled textures with partial updates, streamed
per-frame geometry, frames-in-flight pacing and render-to-texture passes.
`docs/architecture.md` lists GUI as a planned module above Renderer, without
EnCom, keeping its own lightweight hierarchy.

Immediate-mode toolkits make authoring approachable and give the application a
great deal of low-level control, but describe the whole visible interface
again every frame. Retained toolkits keep state between frames but tend to be
heavy and impose an application framework. Truffle GUI aims to combine the
approachable authoring and freedom of the first with the persistence of the
second, without becoming an application framework.

Existing toolkits are inspiration for publicly documented concepts only. No
code is copied from them; resemblance at the level of common ideas is
expected and acceptable.

## Decision

### Names

| Library | Target | Namespace / headers |
| --- | --- | --- |
| Truffle GUI | `truffle_gui`, `Truffle::GUI` | `truffle::gui`, `truffle/gui/` |
| Truffle GUI Dock | `truffle_gui_dock`, `Truffle::GUIDock` | `truffle::gui::dock`, `truffle/gui/dock/` |

These are working names. A distinctive product name may replace them before
the first release.

### Dependencies

```text
GUIDock --> GUI --> Renderer
Renderer --> Shader --> RHI --> Base
Renderer --> RHI
```

- GUI depends on Renderer and uses its batches, clip rectangles, sampled
  textures, stream buffers, frame pacing and passes. It does not call RHI
  directly and does not depend on EnCom, Assets or Engine.
- GUI's built-in shaders ship as prepared TSPs, so the GUI works without a
  shader compiler. A direct Shader dependency is added only if GUI compiles
  shaders at runtime (for example application-supplied effects).
- GUI Dock depends only on GUI. Nothing below GUI depends on it.
- Text libraries are private, optional and pinned dependencies of GUI
  (see Text).

### Persistent element tree

The GUI owns a lightweight tree of elements; it is not an ECS. A panel's
children are its elements. Parents place and clip their children; the tree also
determines drawing order and input routing.

- Elements have stable identities. External code refers to them through
  checked handles, so a removed element cannot be used by mistake.
- Resolved style, layout results and drawing shapes are computed once and kept.
  While nothing changes, none of that work is repeated.
- Anything can change at runtime. A change marks only the work it affects:
  a new color needs repainting; new text needs measuring, layout and
  repainting, and a size change also re-lays out the parents and siblings
  that depend on it.
- Arbitrary application memory is not watched. Changes reach the GUI through
  property updates, binding notifications or an explicit refresh of a subtree.
- Application state stays in the application (documents, selections, domain
  values). GUI state stays in the GUI (focus, hover, capture, scroll, layout).
- Large collections (lists, tables, trees) virtualize: only visible rows have
  live elements.

### Authoring

1. **Object API (core).** Create elements, set properties, add children,
   attach callbacks. Everything else is built on it.
2. **Builder (optional layer).** Concise nested C++ that constructs the same
   objects in one expression. No markup language or code generator is required.
3. **Immediate-style regions (optional layer).** A region whose contents are
   described by application code in the familiar immediate style. The code
   runs when the region is invalidated, or every frame only if the application
   marks the region live. Its output is matched by stable IDs to persistent
   elements, so unchanged elements keep their state and shapes.

### Low-level freedom (goal)

The application should have control comparable to immediate-mode toolkits:
overriding the position and size of any element (the equivalent of
"next window position" or "set cursor position"), elements that supply their
own shapes, free-drawing canvas elements and overlays, and access to an
element's generated shapes. This is a stated direction grown over iterations,
not a first-version maturity requirement. Position overrides and custom
drawing exist from the first version.

### Layout

The first version provides four layout kinds:

- **Row/column:** children one after another with spacing and alignment.
- **Stretching:** children grow or shrink to share leftover space by weight,
  with minimum and maximum sizes.
- **Grid:** rows and columns whose cells line up, with fixed, content-sized or
  weighted tracks and spans.
- **Free placement:** exact position and size relative to the parent.

Layout is a public interface (measure under constraints, then arrange), so
applications can write their own; docking and later layouts use the same
interface. Layout reruns only for invalidated parts of the tree.

### Styling

- A **theme** is a set of named values: colors, spacing, corner radii, borders,
  fonts. Elements refer to these names rather than copying the values.
- Any element may **override** any property directly.
- Changing a theme value updates only the elements that use it.
- Named reusable **presets** (for example a "primary button" style) come later.
- There is no CSS-style selector engine.

### Events

- Callbacks are registered on elements. Input goes to the target element and
  then bubbles up through its parents; a handler can stop it.
- The GUI owns hit testing, focus, pointer capture and keyboard routing.
- Inside immediate-style regions, code may instead poll results
  (for example "was this button clicked").
- An optional debug log records routed events. It is off by default and costs
  nothing when disabled.

### Text

- Truffle GUI defines its own text interface (shaping, measurement, glyph
  rasterization, glyph atlases) so the rest of the GUI does not depend on a
  particular library.
- The first implementation uses FreeType for rasterization and HarfBuzz for
  shaping, as private optional pinned dependencies. Their licenses and any
  bundled fonts are reviewed before adoption.
- Capability grows in steps: basic shaping and an atlas first, then fallback
  fonts, then bidirectional text, editing, selection and input-method
  composition. The interface must not rule out those later steps; characters,
  glyphs and cursor positions are kept distinct.

### Drawing

- Each element keeps its generated shapes (vertices, indices and atlas
  references). Only changed elements regenerate them.
- Each frame, the GUI combines the stored shapes in order into Renderer
  batches through stream buffers, clip rectangles and sampled atlases. The GPU
  redraws the UI; the CPU does not rebuild it.
- Batching never changes visual order.
- Saving whole panels as images, and redrawing only changed screen regions,
  are later opt-in optimizations, added after measurement. The design keeps
  room for them.

### Host boundary

- The host owns windows, the event loop, platform input and frame scheduling.
  It forwards input (pointer, keys, text, input-method composition, scale
  changes) to the GUI and supplies the render target.
- The GUI reports when it needs a frame and when its next animation is due; it
  installs no event loop of its own.
- Clipboard, cursor shape, input-method placement and, later, accessibility
  cross an explicit host interface. Elements keep semantic roles and labels so
  accessibility never has to be inferred from pixels.

### Docking (Truffle GUI Dock)

First version, inside one host window:

- Drag a panel's tab; drop markers offer left, right, top, bottom and center
  (tab). Dropping splits the area or adds a tab. Dividers resize docked areas.
- Layouts (panels, positions, sizes, tabs) are saved to and restored from a
  small text description, including named layout presets.

Separate operating-system windows come later. The host interface is designed
for it from the start: the docking library requests a window and the
application creates it, with its own swapchain.

## Not decided here

Exact API shapes, threading rules, the saved-layout format, animations and
transitions, high-DPI details, the accessibility bridge and the sub-issue
order are settled in #217's implementation sub-issues.

## Consequences

- The GUI gains a scalable persistent model: an idle UI does no layout or
  shape rebuilding, and work after a change is proportional to what it affects.
- Implementation must prove those claims, not only assert them.
- Invalidation bugs (stale layout or shapes) become the main correctness risk;
  tests must cover them directly.
- Immediate-style authoring remains available without forcing every
  application to rebuild every frame.
- Renderer gains no GUI concepts. Any Renderer gaps found while building the
  GUI are fixed in Renderer issues.

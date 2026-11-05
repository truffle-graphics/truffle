# Handoff

This is the rolling execution handoff, not an implementation transcript.
`PROJECT_CONTEXT.md` owns durable direction; `docs/roadmap.md` and the GitHub
issues/Project own accepted work. Earlier phase details remain in their PRs,
the support matrix and Git history.

## Workflow: Batch Branches

Maintainer decision: a branch and its PR cover a related batch of issues with
one signed commit per issue, instead of a branch and PR per issue. Recorded in
`AGENTS.md` and `CONTRIBUTING.md`. Latest batch:
`feat/gui-text-native-docking` (#251-#253), following the three batches
that completed epic #217.

## Latest Delivery: GUI Usability (#251-#253)

The owner approved one `feat/gui-text-native-docking` batch branch/PR,
independently validated signed issue phases with immediate pushes, self-review
and a normal merge commit. Milestone: GUI usability. #217 remains completed;
these are explicit follow-ups, not a reopening of its bounded baseline.

- #251: Unicode layout and ordered font fallback, then visual caret/selection
  geometry used by editing. Private pinned text dependencies remain inside GUI.
- #252: plain-text editing, grapheme-aware mutations, pointer/keyboard
  selection, clipboard, undo/redo and input methods; native Cocoa integration
  stays in the example host.
- #253: checked detached docking surfaces, host-owned native windows and divider
  cursor feedback. GUI Dock remains above GUI; OS window policy stays outside.

#251 is implemented and independently validated: pinned Unicode 16 SheenBidi
2.9.0/utf8proc 2.10.0, script/bidi paragraph runs, ordered font fallback,
source-byte grapheme/caret/selection geometry and GDEF ligature carets.
Missing font coverage retains the primary font's glyph 0; configured fallback
handles stay live until the chain is cleared. All 1,093 matching grapheme
fixtures and upstream bidi/script tests pass, as do text-enabled/disabled
Unicode tests and installed consumers. Dependencies remain private and optional.

#252's object/widget/builder/region editing paths and Cocoa NSTextInputClient
adapter are implemented and validated. Editing covers grapheme-safe mutations,
visual bidi navigation/selection, clipboard, 128 undo/redo steps, read-only and
multiline input, composition cancellation/commit and callback destruction.
Programmatic setters are callback-free; fonts are explicitly selected for the
editor. Native Metal pixels check selection, caret and preedit underline.
The 148-element native demo checks marked text, candidate placement, one-step
commit/undo and exact idle/selective work; nine frames total. Text-disabled
editing reports unsupported explicitly.

#253 adds independent host/input/viewport roots inside one Context, preserving
live ElementIds and panel values across checked detachment and redocking.
Hidden host preparation precedes validated transfer/show; failures preserve the
docked panel. Close, redock, restore, element destruction and Context teardown
clean up surfaces/windows. Divider hover/drag/reset request axis resize cursors.
Hosts retire surfaces after in-flight callbacks return. Saved layouts normalize
detached panels back into docking; OS window geometry is not persisted.
`gui_dock_window` proves two visible Metal swapchains, live detached rendering,
resize, clicks and close/redock/restore cleanup. Its host is macOS-only and has
no IME adapter; `gui_window` supplies the separate native input-method proof.

Final integrated evidence: 16 text-enabled GUI tests, 13 text-disabled GUI tests,
installed package consumer build and execution all pass in the warning-as-error
Debug configurations. The text-enabled set includes both native window demos
and exact Metal editor pixels. The native docking agent also verified the
text-disabled native demo. Architecture and whitespace guards pass. No hosted
workflows were started; this is macOS/Metal evidence, not broader GUI platform
promotion. The batch is delivered as signed, immediately pushed phases with
owner-authorized self-review and a normal merge commit.

## Previous Delivery: GUI Collections And Docking (#234-#235)

#234 adds `truffle/gui/collections.hpp`. `VirtualList` owns one focusable
element whose children are a recycled pool of about `ceil(height/row) + 1`
rows: `make_row` builds a row once, `bind_row` fills it for an index. Its
updater clamps the double-precision offset, unbinds rows that left the range,
grows/shrinks the pool and binds missing indices; rows are placed by
slot-relative `position.y` and the sub-row remainder is a child offset, so a
scroll within a row touches no layout or shapes and 1M rows keep float-safe
positions. `LayoutPass::request_update` lets the rows layout ask for another
update when the arranged height differs; `prepare_frame` then reruns
updates/styles/layout once in the same frame. Wheel (20 px units), arrows,
page and home/end scroll; a primary press focuses and reports `activated`.
`set_count`/`refresh`/`scroll_to` cover data changes. `table_columns` gives
rows a shared fixed/weighted column grid; `flatten_tree` turns expanded nodes
of a `TreeSource` into depth-annotated rows for a list.

Validation: 1,000,000 rows hold 13 elements; in-row scroll binds nothing and
measures/arranges/regenerates nothing; one row of scroll rebinds one row;
deep jump, activation, keys, wheel, `scroll_to`; a taller list grows its pool
in the same frame; count shrink to 0; table cells; tree flattening. 14 GUI
tests pass in the warning-as-error Debug build and 12 in the text-free build.

#235 adds the optional `Truffle::GUIDock` library (`truffle_gui_dock`, above
`truffle_gui`; the architecture guard rejects the reverse). The pure model in
`truffle/gui/dock/layout.hpp` is a tree of tab areas and splits with
fractions; `insert_panel`/`remove_panel`/`move_panel` keep it normalized
(single-child splits collapse, same-axis splits merge) and `move_panel` leaves
the layout unchanged on failure. `serialize`/`parse` use a small
locale-independent text form with shares in parts per 10000; `DockPresets`
stores named layouts. `DockSpace` (`truffle/gui/dock/dock.hpp`) maps the model
onto one element: area backgrounds, active contents, tab strips, dividers and
a drop marker overlay; hidden panels move to a stash. Dragging a tab past a
threshold previews a drop (tab strip = center, near edge = side) and the drop
moves the panel; dragging a divider changes the two neighbouring fractions,
clamped to `min_area`. `DockWindowHost` is declared for future separate OS
windows and is not used.

Validation: layout insert/move/remove/normalize/errors, serialization
round-trip and parse errors, presets; DockSpace bounds after restore, tab
activation, drag preview left/center/bottom and drop result, divider drag and
clamp, presets, remove/undock/dock and destruction. 15 GUI tests pass in the
warning-as-error Debug build and 13 in the text-free build.

## Previous Delivery: GUI Authoring (#232-#233)

#232 adds `truffle/gui/builder.hpp`: plain nested C++ values (`BuildNode`
from `ui::column`, `row`, `grid`, `panel`, `scroll`, `label`, `button`,
`checkbox`, `slider`, `image`) with fluent size/weight/min/position/cell/
padding/disabled/layout modifiers and `bind(Widget&)` outputs. `build` creates
the same elements through `Widget::create` and the Context API; nothing is
retained, there is no markup or generator. Unset fields keep widget defaults;
padding is a property override so it wins over the bound theme value. Children
of a scroll area go to its content. `BuildOptions` supplies a default text
engine/font/atlas. A failure destroys the partial subtree and clears bound
outputs.

Validation: a built tree matches the object-API tree element by element
(bounds, layout params, padding, shapes) and wires callbacks; grid cells and
widget defaults; rollback on a failing descendant.

#233 adds `Context::set_updater`/`request_update`/`set_update_live`: per-element
updaters run at the start of `prepare_frame` (before styles) once when set or
requested, or every frame while live, keep `needs_frame` true while due, stay
pending on failure and die with their element (`updates.cpp`).
`truffle/gui/region.hpp` builds on it: `Region::create` owns a column element
whose body describes `label`, `button` (polled clicked), two-way `checkbox`/
`slider` and nested `row`/`column` items by id per container. Items reuse
their widgets (unchanged setters are no-ops), are reordered with `reparent`,
recreated on kind/range change and destroyed when not described. Widget
callbacks only record the action and request the region's update, so there is
no ownership cycle. Repeated ids fail the frame.

Validation: an unchanged rerun keeps identity with 0 shapes regenerated and 0
measured; no rerun or frame without a request; click/toggle polling and model
sync; insert/move/remove keep surviving elements; live mode; failure stays
pending; updater removal with its element. All 13 GUI tests pass in the
warning-as-error Debug build and 11 in the text-free build.

## Previous Delivery: GUI Layout And Drawing (#230-#231)

#230 adds `GridLayout`: declared columns/rows sized fixed, to content or by
weight (each clamped to min/max), with spacing, implicit content-sized tracks
past the declared ones, and per-child `LayoutParams::cell` (column/row, spans,
horizontal/vertical alignment). Content tracks take their largest single-span
child; a spanning child that does not fit grows its spanned non-fixed tracks
equally. Weighted tracks share the arranged space like stack weights. Children
in only fixed tracks measure bounded; edges round like stacks. Changing one
child re-measures only its path and re-arranges only moved/resized cells.

Validation: exact-rect tests for fixed/content/weighted tracks, spacing,
spans, alignment, implicit tracks and grid measurement, plus incremental
counts (2 measured, 4 arranged; untouched cells keep their shapes).
Ownership: GUI layout only; no new module dependencies.

#231 adds the low-level freedom of ADR 0013. `Context::set_placement` gives
any non-root element an exact parent-relative rect: the parent's layout no
longer sees it (`LayoutPass::children` returns flow children; siblings close
the gap), while the context measures it tightly and arranges it after the
layout. A same-size move only re-arranges the parent. `set_overlay` draws a
subtree after the normal tree (and nested overlays after their container, in
tree order) with only the viewport as clip, and hit tests it first; disabled
ancestors still disable it and scrolling still moves it. `CanvasBuilder`
paints after the decoration and repaints on `invalidate(element,
Dirty::shape)`; `ShapeWriter::triangles`/`line` add solid untextured geometry
(no anti-aliasing; deep inside the SDF so the existing GUI program renders
it). Shapes stay readable through `Context::shapes`. Placement/overlay state
lives in `placement.cpp`, keeping `context.cpp` cohesive.

Validation: tests for placement flow, nested content, same-size move counts,
new/reparented/destroyed children with a placed sibling and invalid inputs;
overlay draw order, clips, scrolling and toggling; overlay hit priority,
disabled ancestors; primitive geometry and canvas repaint. All 11 GUI tests
pass in the warning-as-error Debug build and 9 in the text-free build.

## Previous Delivery: GUI Interaction (#226-#229)

#226 adds GUI-owned topmost clipped hit testing, mutation-safe bubbling,
direct hover/focus/capture notifications, Tab traversal and per-pointer
capture. Disabled subtrees cannot receive host input. Removal forgets checked
input targets; cancellation, final button release and focus loss release capture.
The trace sink is optional with no retained log. Input alone remains frame-free.

Owner: GUI; existing Renderer/Base contracts only, no architectural boundary
change. Routing lives in `events.cpp`, leaving Context's existing tree/layout
implementation cohesive. New public contracts are documented extensions.
Text editing, docking and virtualization remain outside this batch.

#227 adds typed named values, indexed dependent-element bindings, per-property
overrides and cached resolution. Paint-only properties never dirty ancestors;
padding/spacing/font changes invalidate only dependent measurement paths.
Text sources retain UTF-8 and borrow the application's engine/atlas so font
changes reshape only affected text. `FrameStats::status` reports resolution
errors without consuming failed work; manual staging resolves styles first.
Theme resolution is isolated in `theme.cpp`, with no new module dependencies.

Validation: warning-as-error Debug build; event, host, text and theme tests pass.
Theme tests cover exact dirty states/counts, shielding/restoring overrides,
rebinding/removal, invalid values, stack spacing and deterministic font changes
including unloaded-font recovery.

#228 adds checked non-owning Widget references for panel, label, image,
button, checkbox, slider and scroll area. The tree keeps widget state and
semantic roles/labels. Controls bind named theme values; direct property
overrides survive hover/focus changes. Pointer capture, Space/Enter activation,
slider keyboard/drag input and scroll wheel/keyboard behavior use #226.
Programmatic setters never emit user-action callbacks. Input failures surface
through `Context::handle` and `last_event_status`.

Scroll children measure unconstrained and clip to their viewport. A child
translation shared by drawing, hit testing and absolute bounds changes no
stored shapes or layout. Resize/content changes clamp scroll offsets during
arrangement. Immutable slider builders own only shape parameters.

Validation: all nine GUI tests pass in the warning-as-error Debug build,
including interaction tests and native Metal pixels for every widget,
hover/press/release, checkbox/value changes, glyph updates and scrolling.
Ownership remains GUI, with widget behavior in `widgets.cpp`; no lower layer
acquires a GUI dependency.

#229 replaces the boundary-only example with a native core-widget demo:
application composition in `demo.hpp`, host/font/input/presentation in
`main.mm`. All seven widgets and 64 persistent rows live in a 145-element tree.
The smoke run proves zero idle recomputation, exactly two paint-only theme
dependents, zero scroll recomputation and one text change measured along only
four elements. It checks eight frames and every control callback, then idle.
Measured counts and native pixel evidence are recorded in the example README.
The system font remains host-owned and is not redistributed; this example
requires text, while the GUI's text-free build remains supported.

Batch self-review also enforces stable non-copyable/non-movable Context
ownership (Widget callbacks borrow it), clears input targets when reparented
under a disabled ancestor, and reports callback failures through routed input
and direct focus/capture operations. These are tightly coupled lifetime/input
fixes, not new scope. New installed-consumer coverage uses widgets/themes/input.

Validation: warning-as-error Debug; all nine GUI tests, native window smoke and
installed package consumer pass. Text-free event/theme/widget/host/text tests
also pass. No hosted build is requested. Context tree/layout, routing, themes,
widget behavior and native host composition remain separated; no architectural
boundary changes or new runtime module dependencies.

The interactive baseline is complete and ready for the single batch PR to
`develop` with a merge commit. Remaining GUI work is #230-#235 (grid, flexible
positioning/custom drawing, authoring layers, virtualized collections, docking),
not included here. Text editing and broader GUI backend evidence remain outside
this delivered baseline.

## Previous Delivery: TFL_ Prefix (#244)

All `TRUFFLE_*` CMake options, cache/build variables, compile definitions,
preprocessor macros, test macros, preset entries, CI references and docs are
renamed to `TFL_*` (147 names) without compatibility aliases. Targets,
namespaces, headers and the `Truffle` package/`Truffle::*` targets are
unchanged; vendor sources are untouched. Existing build directories must be
reconfigured from a clean cache because old option names are not migrated.
The convention is recorded in `CONTRIBUTING.md`.

Validation: clean default configure/build with warnings as errors and all 50
default tests pass (Metal, GUI text on, installed package consumers); the
`dev` preset configures with no unused-variable warnings and no `TRUFFLE_`
cache entries; architecture guard and `git diff --check` pass. Only the
intentional `GL_TRUFFLE_missing_extension` test extension name keeps the old
spelling. Next: GUI event routing, hit testing, focus and pointer capture
(#226).

## Previous Delivery: GUI Text Default And Atlas Access (#243)

Follow-up to #225 (merged in PR #242, `ad601d5`), epic #217.

- `TFL_GUI_ENABLE_TEXT` now defaults to ON: text is fundamental to the
  GUI. Configure fails with preparation instructions when the FreeType/HarfBuzz
  submodules are missing; `-DTFL_GUI_ENABLE_TEXT=OFF` keeps a text-free
  GUI. The manual Release workflow prepares only those two submodules.
- `TextEngine::atlas_texture()` returns
  `Result<std::reference_wrapper<SampledTexture>>`: `invalid_state` for a
  default-constructed or moved-from engine (previously a null dereference) and
  `unsupported` when text is compiled out (previously a dummy texture).

Validation: text-on `truffle_gui_text_tests` (default, moved-from and
shaping cases) and `truffle_gui_drawing_metal_tests` pass; text-off
`truffle_gui_text_tests` passes; a clean default configure selects text ON.
Architecture guard and `git diff --check` pass. Delivered in PR #245.

## Previous Delivery: GUI Text (#225)

Fifth GUI step of epic #217 (ADR 0013): optional text shaping, glyph
rasterization and persistent-tree integration.

- `gui/text.hpp`: `TextEngine` loads copied font bytes, shapes UTF-8 through
  HarfBuzz, rasterizes FreeType glyphs, exposes ascent/descent/line metrics,
  glyph positions/clusters and an RGBA atlas texture. Newly encountered bitmap
  glyphs update only their packed atlas rectangles; repeated glyphs reuse the
  cache. The engine is single-thread-owned and the RHI device must outlive it.
- `Context::set_text`/`clear_text`: text contributes to intrinsic measurement
  and generates element-local atlas quads. Text changes invalidate measure,
  color-only changes invalidate shapes, and unchanged text does no shaping or
  rasterization. The atlas is registered with `Painter` by the application.
- Text was initially disabled by default (changed by #243). FreeType 2.14.3 (FTL, not its GPL alternative)
  and HarfBuzz 14.5.1 (Old MIT) are private pinned dependencies. No font is
  bundled. Enabled installs include their static libraries, CMake packages,
  license texts and the required FreeType acknowledgment; disabled GUI
  packages do not acquire either dependency.
- Scope deliberately excludes fallback fonts, bidirectional text, editing,
  selection, composition and atlas eviction; these remain later capabilities.

Validation: `truffle_gui_text_tests`, `truffle_gui_drawing_metal_tests`,
`truffle_gui_layout_tests` and `truffle_gui_drawing_tests` pass in the enabled
Debug build with warnings as errors. Text-disabled text/layout tests and the
installed package consumer pass; the enabled package consumer also passes and
confirms the FreeType acknowledgment is installed. The architecture guard and
`git diff --check` pass. `clang-format` is unavailable locally. Delivered in PR #242.

## Previous Delivery: GUI Host Boundary (#224)

Fourth GUI step of epic #217 (ADR 0013): the host-owned boundary.

- `gui/input.hpp`: host-facing `PointerEvent`, `WheelEvent`, `KeyEvent`,
  `TextEvent`, `CompositionEvent` and `FocusEvent` (variant `InputEvent`),
  `Modifiers`, `Key` and the aggregated `InputState` (pointer, buttons,
  modifiers, focus, composition). Routing to elements is #226.
- `gui/host.hpp`: `Host` services with idle defaults: cursor, clipboard and
  text-input (IME) placement. `Context::host()` never returns null.
- `Context`: `set_viewport(size, scale)`, `handle(event)` (updates input state
  only, never requests a frame), `request_frame()`, `request_frame_at(time)`,
  `needs_frame(now)`, `next_update()` and `prepare_frame(now)` (layout,
  shapes, clears dirt, the request and due timers). `Painter::record` now
  takes the content scale from the Context.
- `examples/gui_window`: a Cocoa window that translates AppKit events, sleeps
  until input or the next timed update, and draws only when the GUI needs a
  frame. Cursor and clipboard are real; IME placement waits for text editing.

Validation: `truffle_gui_host_tests` (exactly one frame per change, idle,
viewport and scale validation, timers, input state, host services) and the
`truffle_gui_window --smoke` ctest (1 initial frame, idle, 1 timed update,
pointer movement draws nothing, marker placement and window resize each draw
one frame, 12 decorated elements) pass in the local Debug `build/gui` tree
with Metal validation, along with the other GUI and Renderer GUI primitives
tests. `clang-format` is not installed locally; the package-consumer test was
not rerun.

## Previous Delivery: GUI Drawing (#223)

Third GUI step of epic #217 (ADR 0013): persistent shapes and Renderer drawing.

- `gui/shapes.hpp`: `Decoration` (fill, border colour/width, corner radius,
  `TextureId`, UV rectangle), `ElementShapes` (72-byte `ShapeVertex`, uint32
  indices, per-texture segments), `ShapeWriter::rect` and an overridable
  `ShapeBuilder` for application-drawn shapes. Shapes are element-local and
  are regenerated by `Context::update_shapes()` only for shape-dirty elements
  (decoration/builder change or resize); moves never regenerate them.
- `gui/draw.hpp`: `assemble()` walks the tree in pre-order, translates stored
  shapes to clip space (DPI `scale`, y up or down), intersects parent clips
  when `clips_children` (default) and skips wholly clipped subtrees. Adjacent
  shapes with equal texture and clip share one command.
- `gui/painter.hpp`: `Painter` turns a frame into Renderer batches through a
  `StreamBuffer`, a shared `TextureBindingLayout`, a white default texture and
  registered `SampledTexture`s. The GUI program (`src/gui/shaders/gui.metal`:
  rounded-box SDF coverage, border blend, premultiplied alpha) is embedded at
  configure time with its SHA-256 and packaged in memory, so no shader compiler
  runs. Metal only for now; other devices return `unsupported`. The host
  supplies the render target, pass and frame slot.

Without remaps, Metal maps a native binding to slot `group * 8 + binding`, so
the GUI sampler (binding 1) is `[[sampler(1)]]`. Overlays escaping clips are
#231; DPI and the host window landed in #224.

Validation: `truffle_gui_drawing_tests` (regeneration counts, rectangle
geometry and clamps, builder segments, assembly order, clips, merging, scale
and moves) and `truffle_gui_drawing_metal_tests` (Metal validation layers;
four frames of 32x32 pixels matched against a CPU reference within 2/255 for
colours, borders, translucency, draw order, parent clipping, rounded corners
and textures, plus unchanged, redecorated and moved frames) pass in the local
Debug `build/gui` tree, along with the element-tree, layout and Renderer GUI
primitives tests. `clang-format` is not installed locally; the
package-consumer test was not rerun.

## Previous Delivery: Renderer Texture Binding (#238)

Renderer gap found while starting GUI drawing (#223): sampling a
`SampledTexture` required direct RHI calls, which ADR 0013 rules out for GUI.
`renderer::TextureBindingLayout` (texture binding 0, sampler binding 1, plus a
pipeline layout) and `renderer::TextureBinding` (owned sampler and bind group,
`batch_group()`) close it. The Null primitives test covers creation, moves and
invalid inputs; the native Metal GUI-primitives proof now uses the helper with
unchanged exact pixel checks. Both pass in the local Debug `build/gui` tree,
now configured with Metal and the Metal shader compiler.

## Previous Delivery: GUI Layout (#222)

Second GUI step of epic #217 (ADR 0013): a public measure/arrange layout
interface on the persistent element tree.

- `gui/geometry.hpp`: `Size`, `Point`, `Rect`, `Edges` and `Constraints`
  (`unbounded` maximums).
- `gui/layout.hpp`: per-element `LayoutParams` (optional fixed size, min/max,
  padding, stretch weight, free position); an abstract `Layout` that
  applications can implement through a `LayoutPass`; built-in `StackLayout`
  (row/column, spacing, main/cross alignment, weighted stretching that freezes
  at min/max, whole-unit edges) and `FreeLayout` (the default, explicit
  positions). Layouts are immutable; replace one to reconfigure.
- `Context::update_layout(viewport)` measures top-down with cached constraints
  and arranges only arrange-dirty subtrees; bounds are parent-relative, so a
  moved element keeps its subtree and shapes. Only resized elements gain
  `shape`. A position- or weight-only change re-arranges the parent alone.
  `LayoutStats` reports measured/arranged counts; an idle update does no work.

Grid layout is deferred to #230. Not thread-safe.

Validation: `truffle_gui_layout_tests` (stack padding/spacing/alignment,
weighted grow and shrink, min/max, free placement, custom layout, incremental
counts, viewport change, stale handles) and `truffle_gui_element_tree_tests`
pass in the local Debug `build/gui` tree with warnings as errors.
`clang-format` is not installed locally; the package-consumer test was not
rerun.

## Previous Delivery: GUI Element Tree (#221)

First GUI implementation step of epic #217 (ADR 0013). Adds the
`truffle_gui` / `Truffle::GUI` target depending only on Renderer, with its
architecture-guard rule and package-consumer link check.

- `truffle::gui::Context` owns a persistent element tree with a fixed root:
  create, destroy (whole subtree), reparent and reorder children.
- `ElementId` is a `base::Handle` packing slot index and generation; removed
  handles are detected and rejected, and reused slots get new generations.
- Dirty flags `style`, `measure`, `arrange` and `shape` are independent.
  `measure` propagates `measure|arrange` to ancestors (stopping at an already
  measured ancestor); `arrange` adds own `shape`; `style` and `shape` stay
  local; structural changes mark the affected parents.
- `dirty_elements()` lists only live dirty elements, and `clear_dirty(flags)`
  clears selected stages.

Validated by `truffle_gui_element_tree_tests` and the architecture guard.

## Previous Delivery: Organization Project Planning (#219)

The delivery tracker is now the organization-owned
[Truffle Project](https://github.com/orgs/truffle-graphics/projects/1).
All 124 existing items were migrated; the personal Project is closed, not
deleted. Module-owned epics are top-level; #25 is closed core-foundation
history. Fields are Status (Backlog, Next, In Progress, In Review, Done),
Module, Priority, Size and Target evidence; phase labels and Phase/Workstream
fields are retired.

Five saved views provide a priority-sorted Work board for all unfinished work,
module hierarchy (By module), epic progress (Epics), Backlog and Done.
The owner removed the redundant Next up view. Work board keeps Module slicing,
deferral labels and parent links on cards; filters use supported qualifiers.
The `epic` label includes organizing issues awaiting their first children, so GUI remains
visible in the overview.

This documentation-only delivery updates the four active tracker links and
the issue-template contact link, and aligns planning/roadmap guidance with
the live Project. No production contract, dependency, backend maturity or
version changes. The owner approved one signed
commit with immediate push and a review-first PR into develop.
GUI architecture PR #218 is merged separately; no GUI implementation is included here.

Validation: documentation diff, whitespace, obsolete-link search and the
lightweight architecture guard pass. GitHub readback confirms the old Project
is closed and #25 is Done. Browser readback confirms five saved views without
filter warnings.
Next: decompose GUI epic #217 into implementation sub-issues.

## Previous Direction: Truffle GUI Architecture (#217)

The owner and agent designed the GUI module interactively; the decisions are
recorded in [ADR 0013](adr/0013-truffle-gui-architecture.md). This is a
docs-only change: no GUI code, CMake target or dependency exists yet.
Working names are Truffle GUI (`Truffle::GUI`) and Truffle GUI Dock
(`Truffle::GUIDock`). Key choices: persistent element tree with scoped
invalidation; object API core with optional builder and immediate-style
regions; theme values plus per-element overrides; bubbling callbacks; own text
interface over FreeType/HarfBuzz (license review first); elements keep their
shapes; row/column, stretching, grid and free layouts from the start; docking
within one window first, OS windows later via a host interface.

Next: decompose epic #217 into implementation sub-issues with Project fields,
agreeing scope, order and delivery mode with the owner before production code.

## Previous Delivery: Minimum Renderer (#206)

Three owner-approved signed/pushed slices, merged after owner-authorized
self-review: data/mesh #207, optional view/depth #208, host-window example
#209. They stack on ADR PR #205. The minimum Renderer is the only accepted next
scope; GUI and later applications are not planned here.

#207 (PR #210) adds explicit, reflection-checked vertex layouts, owned immutable
mesh byte uploads and batch-range validation. Renderer owns those mechanisms; Shader/RHI
retain their lower-layer responsibilities. ECS semantics, buffer-member packing
and lighting remain application-owned. Separate mesh upload, layout validation,
and orchestration files keep ownership cohesive. Nine focused Renderer, EnCom
adapter and frame-graph tests pass with warnings-as-errors, including native
Metal uploaded indexed-mesh exact 256-pixel proof under validation. The final
native rerun also covers indexed indirect buffer binding and rejection of
vertex-layout-changing replacement. Architecture guard and whitespace checks pass.

#208 (PR #211) adds optional camera math and depth allocation/transition helpers. The
shared minimum-renderer application fixture owns its vertex/instance/view/light
types, composed MSL shader chunks and explicit completion fence. Renderer does
not acquire those semantics. View/depth contract tests and the Metal offscreen
proof pass. The latter checks all pixels for occlusion in either instance
order, app properties, light direction/intensity, quaternion interpretation,
camera translation, perspective, and resized 32x24 targets under validation.

#209 adds a real Cocoa/CAMetalLayer host example, two rotating lit cubes and
swapchain-pass depth/clear preservation. The host owns window/input/backing
size/resize/out-of-date recovery; Renderer owns no platform window policy.
The smoke presents 12 frames with two actual drawable/depth resizes. The same
cube geometry is checked against an exact spatial offscreen reference.
Scene completion fencing also runs after recoverable presentation errors so
CPU updates cannot race an earlier successful submission. Separate scene
data/shading and native window-host files remain cohesive example owners.

Final focused evidence: 12/12 native-enabled Renderer/EnCom/frame/window tests,
plus 4/4 compiler-disabled data/view/source-program/prepared-Metal tests.
The installed compiler-off package consumer also builds and runs with the new
Mesh/layout/view/depth public headers and functions.
Warnings-as-errors builds pass. The architecture guard checks 17 production
targets, 25 dependency declarations and 104 public-header imports. No backend
maturity, cross-platform claim or version bump is implied.

## Pre-GUI Renderer Fixes (#215)

The owner chose two Renderer fixes before starting the GUI module and authorized
self-review and merging. Native MSL packages (every source MSL) no longer need
`allowExperimentalMetalGeneration`; cross-compiled Metal stays gated, and the
compiler marks native MSL routes non-experimental. `RenderPassNode::transition_before`
records caller-stated texture transitions before a pass, enabling
render-to-texture layers. Evidence: exact-pixel layer proofs on Metal (native MSL,
validation) and Vulkan (validation layer; fails without the transition), plus
Null contract and package tests. GUI, fonts, input and platform layers are not
started; per-batch draw cost stays deferred.

## GUI Readiness (#213)

The owner asked whether the minimum Renderer is ready for a GUI module and then
authorized implementing the gaps, self-review and merging. Added: per-batch clip
rectangles on dynamic-scissor programs, `SampledTexture` upload/update,
`StreamBuffer` per-frame bytes and `FramePacer` frames-in-flight over a
monotonic fence signaled by `Renderer::render`. Blending was already program
state. `Pipeline::dynamic_state()` exposes the declared RHI state; the cache now
accepts only the scissor dynamic state. GUI, fonts, input and platform layers
are not started. Evidence: 51/51 native tests (including the new exact-pixel
Metal proof under validation) and 41/41 compiler-off tests with the extended
package consumer, all with warnings as errors. Multi-value fence pacing is
verified on Metal.

## Render Data Direction (#204)

[ADR 0012](adr/0012-app-defined-render-data.md) records the accepted
direction: fixed renderer concepts (mesh, buffer, texture, program, pass,
view/camera, draw; optional Renderer camera helpers) stay fixed, while
per-object data, its interpretation, lights and materials are
application-defined, matched to Shader reflection and uploaded unchanged.
ECS/scene types stay outside the Renderer. The legacy `ChannelKind`
and `renderer/components.hpp` remain unchanged for existing integrations; the
new path does not require them. The owner will review the architecture after
the minimum implementation, rather than committing to future phases.

## Previous Delivery: Shader Completion (#73, #193)

The agreed Shader baseline is implemented and accepted. #193 and parent #73
are closed/Project Done after PR #203 merged into develop. That delivery did
not add Renderer features; the separately accepted work above now does.

| Phase | Accepted delivery |
| --- | --- |
| Shared library/tools and direct Renderer use (#178-#181) | Independent Shader, TSP assembly/inspection, prepared programs and `SourceProgram` compile/rebuild adoption |
| Route/target contracts (#185) | PR #194 |
| Canonical reflection/workgroup specialization (#186) | PRs #195/#196 |
| Portable Metal/GL generation (#187) | PR #197 |
| Native MSL source/offline libraries (#188) | PR #201, merge `f833cafffd56753e0b4d74be23f2607ca6afc2ea` |
| HLSL/DXC (#189) | PR #199, public acceptance merge `5852204c9f49c933c6a6fbbd6e1b3b560ac93bac` |
| Slang/native modules (#190) | PR #200, merge `990f7e679f41f1dc9d55c7d5a31e3acbbac87381` |
| WGSL/Tint (#191) | PR #198, merge `f2079307fd2cd5432d465c083cc0c416e0b1ec5f` |
| Shared composition/cache/rebuild/tool audit (#192) | PR #202, merge `0e1bcb9ffc9fd11797a31eee02f8abb2fb590e88` |
| Final evidence/license/export/status audit (#193) | PR #203, merge `cf6836c2c1fb38097388b6e6b9d4ce7aabab58b2`; no remaining accepted Shader implementation phase |

The [evidence-linked Shader matrix](../src/shader/README.md#completion-scope-and-evidence-73-193)
defines supported routes, profile/API floors, experimental gates and reflection
limits. Completion is not every upstream language feature, every conversion
pair, or backend conformance. Existing explicit unsupported combinations are
not relabeled as implemented.

## Ownership And Consumer Contract

- Dependency direction is Renderer -> Shader -> RHI -> Base, without an
  adapter module. RHI/backends never depend on Shader or compiler tools.
- Shader owns source resolution/composition, requested configurations,
  optional compilation, canonical reflection, dependencies, instance-owned
  in-memory cache and explicitly triggered rebuilds.
- Renderer owns GPU resources, compatibility and safe publication. Prepared
  TSP and in-memory compilation results share the same RHI package contract.
- Compiler adapters remain individually optional. Disabling shaderc does not
  disable library compilation; disabling all adapters leaves installed
  Shader/Renderer/RHI consumers compiler-free.
- No custom shader language, mandatory watcher, hidden service, persistent
  cache policy or material/asset system was introduced by Shader completion.
- TSP remains an evolving unreleased `1.0` draft. No version bump, migration,
  compatibility alias or legacy reader is part of this work.

## Native MSL Acceptance And Review Disposition

Native MSL uses ordered `sourceUnits` plus the root and a typed canonical
`interfaceDeclaration`; Metal owns preprocessing. Runtime source reflection
requires macOS 26+ and a Metal device. This does not raise prepared-TSP
consumption's runtime floor.

Observable stage, vertex inputs, resource access/kind/count/size/native slots
and function constants are checked. Logical slots, unavailable IO/interpolation,
push layout, defaults and workgroup details remain explicit caller assertions.
Reserved push buffer 30 must be read-only. Arbitrary project includes are not
promised by the in-process Metal API; select reusable units explicitly.

Offline Metal dependency scanning runs before every cache lookup and rejects
active dependencies outside the installed Metal Toolchain. The installed
compiler/AIR linker preserves reflection; the exact emitted metallib is loaded
and reflected before publication. Source/offline target-conditional mismatch,
newly appearing include dependencies, concurrent compiler processes, continued
directives, writable pushes and ambiguous CLI declarations have regressions.

Cohesion review: `metal_adapter.mm` owns one native request's assembly,
compile/reflection/declaration validation and publication lifecycle.
Distinct tool discovery, dependency scanning and process/pipe/reaping details
are extracted to the private Shader-owned `metal_offline_toolchain` helper.
The length signal is not a God-file verdict or reason for arbitrary splitting.

Native evidence used Apple M3 Pro, macOS 27.0.1, Xcode 27.2 and Apple Metal
compiler 32023.922 / installed Metal Toolchain. Both composed source and offline
libraries produced 256 exact RGBA `(64,64,32,255)` pixels and compute values
`10,12,14,16` (defaults) / `15,18,21,24` (overrides). Toolchain layout/version
requirements are explicit, not guarantees for arbitrary SDKs or older systems.

## Shared Mechanisms And Final Packaging Audit

The standalone `examples/shader_workflow` links only `Truffle::Shader`,
in-tree or installed. It exercises three-source native GLSL composition,
explicit configurations, cache reuse, compile/reflection rejection and cached
recovery without a backend, Renderer, GPU or shaderc.

Native compiler regressions reject observable reflection mismatch and
overlapping canonical push ranges, repeat failed requests without cache
success, recover valid cached output and retain loadable last-good packages
after cache clearing/compiler destruction. Failures never masquerade as
successful last-good results.

The #193 install audit found glslang's license missing from its exported
compiler closure. Shader now installs the pinned `LICENSE.txt` under the
configured data directory; installed-consumer smoke compares its SHA-256 with
the source notice. Other enabled provider/nested notices were checked.
[Dependency policy](rhi1/dependencies.md#shader-tooling) and
[Slang source closure](shader-slang.md) record exact pins, licenses, offline
acquisition and update requirements. Apple frameworks/toolchains are host
prerequisites and are not redistributed.

CLI/library compilation mechanisms are shared. Compilation/reflection/package
failures occur before opening an existing CLI output; this is not a general
filesystem transaction or crash-recovery guarantee.

## Validation Evidence

- Accumulated native/provider acceptance: 29 focused enabled checks with
  warnings-as-errors, plus nine exact final-MSL compiler-off checks.
- Integrated #192: seven enabled workflow/compiler/route/native/CLI/Renderer/
  installed-consumer checks and eight compiler-off package/tool/consumer checks.
  Enabled and disabled standalone installed examples build; unavailable
  compilation returns a real nonzero failure.
- Final install correction: enabled and compiler-disabled installed-consumer
  smoke each pass, including exact glslang notice comparison when enabled.
  The enabled install also passes with a custom data directory. No new
  compiler source build or full platform matrix was needed.
- Architecture guard: 17 production targets, 25 target dependency declarations,
  95 public-header imports. No reverse dependency or public-contract change.
- Compiler-off installed prepared consumer links only system C++/OS libraries;
  enabled installed shaderc executes without loader override variables.
- Changed-document local link targets and `git diff --check` pass.

Tests and PR evidence are local Apple Silicon results. No hosted workflows,
automatic AI reviews, cross-platform promotion or version bump were requested.

## Other Baselines And Outstanding Evidence

Metal macOS (#69, PR #174), Vulkan/MoltenVK (#56, PR #170), and macOS OpenGL
4.1 presentation/synchronization (#61/#63 macOS parts, PR #176) provide the
practical Apple Silicon baseline. They remain `native_smoke`, not full
cross-backend conformance; Null is validation-only.
See the [support matrix](rhi1/support-matrix.md) for precise native receipts.

- New Shader DXIL consumption on Windows/D3D12: #122.
- Current provider/output revalidation on Linux Vulkan: #124.
- Generated GL/GLES output on Linux, including native ES 300: #125.
- Browser WGSL/WebGPU output consumption: #123, after its backend exists.
- Vulkan Windows (#58), remaining desktop presentation/context work (#61/#63),
  Apple mobile lanes (#71/#72), and MoltenVK packaging/iOS (#172) stay tracked.
- Metal timestamp/occlusion pools remain #175; asynchronous submission,
  allocation, recovery, advanced GPU features and full conformance remain
  separate Phase 6/7 work. Synchronous correctness is not throughput evidence.
- macOS targets Apple Silicon, not Intel Macs (#171). Physical GPU removal is
  not an Apple Silicon requirement; device-loss fault injection is not
  physical-removal evidence.

Other-platform verification does not block this Shader baseline. Old backend
smoke runs do not establish native use of the newly added Shader outputs.

## Resume And Delivery Rules

1. Review the minimum Renderer stack (#206-#209), beginning with ADR PR #205
   then #210 and #211 and the host-window PR. Do not merge automatically.
   The implementation and evidence are local Apple Silicon; architecture
   review remains open. Do not start GUI or application phases without scope.
2. Preserve native worktrees/branches/build assets; this audit authorizes no
   branch deletion or history rewriting. Use signed issue-scoped commits,
   immediate same-branch pushes and reviewed normal merge commits.
3. Keep validation local and affected-area focused. Hosted Build/Release and
   cloud/AI companions remain off unless explicitly requested (#131).
4. Runner setup (#165) and retained action-runtime modernization (#132) remain
   separate deferred work. No release or stable-branch promotion is implied.

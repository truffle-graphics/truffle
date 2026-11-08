# GUI Graphics Integration (#279)

`gui::Context` owns the persistent element tree, input/focus state, host
services, viewport and frame scheduling. It is usable without Renderer or
native presentation. `gui::Graphics` is an optional owner for Truffle's device,
Presentation, Renderer, GUI painter and GPU resources. The host owns the native
window, event loop, input translation and application lifetime.

```cpp
#include <truffle/gui/graphics.hpp>

auto created = truffle::gui::Graphics::create({
    .window = nativeWindow,
    .pixels = drawablePixels,
    .dpi_scale = pixelsPerLogicalUnit,
});
if (!created.ok()) {
    // Handle unsupported setup or the reported failure.
}
auto graphics = std::move(created).value();

truffle::gui::Context ui;
ui.set_host(&platformServices);
ui.set_viewport(
    {drawablePixels.width / pixelsPerLogicalUnit,
     drawablePixels.height / pixelsPerLogicalUnit},
    pixelsPerLogicalUnit);
// Build ordinary elements/widgets/menus in ui.
// Forward translated input to ui.handle(event).
// Report resize/DPI changes with graphics.resize(ui, drawablePixels, scale).
if (graphics.needs_frame(ui)) {
    auto status = graphics.draw(ui); // check; do not disguise recoverable errors
}
```

Declare Graphics before Context when Context text sources use Graphics-owned
fonts or textures: Context elements refer to those resources, and Graphics
must outlive those references. Otherwise Context's lifetime and use do not
depend on Graphics.
Wait for submitted work before destroying the native window.

`WindowHandle`/`WindowKind` are GUI/Base values. Windows supplies HWND in
`handle` and optional HINSTANCE in `display`; macOS supplies CAMetalLayer, not
NSWindow or NSView; XCB retains its existing connection/window convention.
SDL or another host obtains those handles itself. GUI adds no host-framework
dependency. Native setup is enabled by `TFL_BUILD_RENDERER_PRESENTATION` plus
the enabled Metal/Vulkan backend. When unavailable it returns `unsupported`;
it never creates Null or quietly changes a failed backend.
`GraphicsBackend`/validation apply to owned setup; a borrowed device is already
configured by its owner.

The root stays transparent and the default pass clears transparent. The host
chooses a different `GraphicsDesc::clear` when wanted; no background widget is
introduced. `needs_frame` is false while minimized, preserving pending work. A
direct `draw` while minimized reports `out_of_date`. Otherwise an unchanged
tree does no work. Sleep until host input/resize or `ui.next_update()`.
Resize/recreation, suboptimal/out-of-date, timeouts, surface/device loss and
recording errors retain Presentation's typed recovery semantics. Replace a
lost surface with `replace_window`; a lost device requires new GPU consumers.

## Images and text

`add_image(context, desc)` accepts RGBA8/BGRA8 pixel bytes, size/row pitch and
nearest/linear sampling, returning a GUI `TextureId`. Use it in a normal
Decoration/widget. `update_image` updates a pixel rectangle;
`remove_image` removes registration. Create/change registrations before
opening a frame and after discarding any unsubmitted recorded batches. Do not
reuse a stale graph after image removal or streamed-slot reuse. GPU-native
resources from submitted commands remain retained by RHI.

`load_font(bytes)` uses Truffle's default 14 logical-unit size, or accepts an
explicit logical size. It lazily initializes the optional text engine and
atlas. `set_text(context, element, font, utf8)` uses that engine/atlas
automatically. `text()` and `text_atlas()` expose GUI-level
shaping/fallback/text-edit composition without exposing Renderer or RHI types.
Supply licensed font bytes; no font discovery or bundled font is imposed.
The [styling guide](gui-styling.md#fonts) describes shared and component font
sizes and the lifetime of engine-owned sized variants.
Text-disabled configurations explicitly report unsupported for text creation.
Initialize text/images before frame preparation; ordinary layout/text updates
use the existing engine.

## Shared Renderer path

Include `truffle/gui/rendering.hpp` only when the application also renders.
`gui::Rendering::create(renderer::Device&, desc)` borrows/shares the device and
owns the GUI-side Presentation, cache, Renderer and Painter. Normal Renderer
resources created from that device retain its owned storage. A raw device can
be imported separately by specialists with `renderer/rhi_interop.hpp`; that
raw object must remain alive and unmoved.

`Rendering::device`, `programs`, `renderer` and `presentation` expose the
shared Renderer-level owners. Scene work can register its own prepared
programs and append offscreen passes to the same `FrameGraph`; do not
overwrite or invalidate `Rendering::gui_program`. `begin_frame(graphics, ui)`
obtains one safe slot and prepares the supplied Context.
`append_gui_pass(graphics, ui, ...)` records/composes GUI without application
knowledge of its pipeline/buffers, and `present(graphics, ui, graph)` submits
the combined graph. Pair an abandoned frame with `cancel_frame(graphics, ui)`.
The same Context must be supplied throughout an open frame. `record` supports
an optional Surface belonging to that Context; independently paced windows
require independent Painter/Graphics streamed resources. Do not mix direct
Presentation frame control with an open Graphics-managed frame.

`gui::Viewport` remains GUI-only for layout/input. Use the
[Renderer-level viewport composition](gui-viewport.md) for application scene
batches: prepare, append scene, display, append GUI, composite and finish from
actual submission statistics. Viewport targets retain exact allocations
across resize/release; GUI still owns its program/bindings.

One Graphics object owns one GUI stream. Context is supplied explicitly to
operations; no Graphics object owns a Context or retains the association after
the call/frame. The optional Surface argument does not make two records into
the same slot safe. A direct Graphics draw and a borrowed Renderer frame use
the same explicit Context contract.

Per-frame graphs retain the Graphics GPU wrapper state. Discard graphs and
call/check `wait_idle` before tearing down Graphics and its borrowed
Host/window. Callbacks, external Context references and borrowed raw devices
follow their existing lifetimes.

The [GUI-owned example](../examples/gui_presentation/gui_graphics.hpp) and
[borrowed example](../examples/gui_presentation/shared_device.hpp) are
compile-checked. Raw Painter, text atlas creation/access and Surface recording
remain explicit in `gui/specialist/`. Focused contracts cover invalid setup
and the Context-only path without a native window. Native owned/borrowed GUI
execution, Metal compilation, package configurations and multi-window
integration remain owner-run unless separately verified.

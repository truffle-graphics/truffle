# GUI Application (#276)

Include `truffle/gui/application.hpp` and link **`Truffle::GUI` only**. GUI owns
its graphics device, Presentation, Renderer, prepared GUI program/cache, Painter
and any images or font atlas created through this entry point. The application
still owns its window, loop, platform services and input translation.

```cpp
#include <truffle/gui/application.hpp>

auto result = truffle::gui::Application::create({
    .window = nativeWindow,
    .pixels = drawablePixels,
    .dpi_scale = pixelsPerLogicalUnit,
    .host = &platformServices,
});
// Check result before moving/using its value.
auto app = std::move(result).value();
auto& ui = app.context();
// Build ordinary elements/widgets/menus in ui.
// Forward translated events with app.handle(event).
// Report resize/DPI changes with app.resize(drawablePixels, scale).
if (app.needs_frame()) {
    auto status = app.draw(); // check; do not disguise recoverable errors
}
```

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
chooses a different `ApplicationDesc::clear` when wanted; no background widget
is introduced. `needs_frame` is false while minimized, preserving pending
work. A direct `draw` while minimized reports `out_of_date`. Otherwise an
unchanged tree does no work. Sleep until host input/resize or `next_update()`.
Resize/recreation, suboptimal/out-of-date, timeouts, surface/device loss and
recording errors retain Presentation's typed recovery semantics. Replace a
lost surface with `replace_window`; a lost device requires new GPU consumers.

## Images and text

`add_image` accepts RGBA8/BGRA8 pixel bytes, size/row pitch and nearest/linear
sampling, returning a GUI `TextureId`. Use it in a normal Decoration/widget.
`update_image` updates a pixel rectangle; `remove_image` removes registration.
Create/change registrations before opening a frame and after discarding any
unsubmitted recorded batches. Do not reuse a stale graph after image removal
or streamed-slot reuse. GPU-native resources from submitted commands remain
retained by RHI.

`load_font(bytes, logicalSize)` lazily initializes the optional text engine and
atlas. `set_text(element, font, utf8)` uses that owned engine/atlas automatically.
`text()` and `text_atlas()` expose GUI-level shaping/fallback/text-edit
composition without GPU setup. Supply licensed font bytes; no font discovery or
bundled font is imposed. Text-disabled configurations explicitly report
unsupported for text creation. Initialize text/images before frame preparation;
ordinary layout/text updates use the existing engine.

## Shared Renderer path

Include `truffle/gui/rendering.hpp` only when the application also renders.
`gui::Rendering::create(renderer::Device&, desc)` borrows/shares the device and
owns the GUI-side Presentation, cache, Renderer and Painter. Normal Renderer
resources created from that device retain its owned storage. A raw device can
be imported separately by specialists with `renderer/rhi_interop.hpp`; that
raw object must remain alive and unmoved.

`Rendering::device`, `programs`, `renderer` and `presentation` expose the
shared Renderer-level owners. Scene work can register its own prepared programs
and append offscreen passes to the same `FrameGraph`; do not overwrite or
invalidate `Rendering::gui_program`. `begin_frame` obtains one safe slot and
prepares GUI. `append_gui_pass` records/composes GUI without application
knowledge of its pipeline/buffers, and `present` submits the combined graph.
Pair an abandoned frame with `cancel_frame`. `record` supports an optional
Surface belonging to the same Context; independently paced windows require
independent Painter/Application streamed resources. Do not mix direct
Presentation frame control with an open Application-managed frame.

Per-frame graphs retain the Application's GPU wrapper state. Discard graphs
and call/check `wait_idle` before tearing down the Application and borrowed
Host/window. No destructor hides a failed wait. Callbacks, external Context
references and borrowed raw devices follow their existing lifetimes.

The [GUI-only example](../examples/gui_presentation/gui_application.hpp) and
[borrowed example](../examples/gui_presentation/shared_device.hpp) are
compile-checked. Raw Painter, text atlas creation/access and Surface recording
remain explicit in `gui/specialist/`. The focused application contract verifies
negative setup/minimization boundaries without a native window; Null drawing
is explicitly unsupported. Native owned/borrowed GUI execution, Metal
compilation, package configurations and multi-window integration were not run.

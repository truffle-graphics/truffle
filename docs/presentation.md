# Renderer Presentation (#270)

`renderer::Presentation` owns graphics-side surface/swapchain lifetime and
frames-in-flight pacing. The host still owns its window, native handles, event
loop, input, DPI reporting and platform services. No SDL, GLFW or Engine
dependency is introduced.

## Native setup without backend headers

Link `Truffle::GUI` and `Truffle::RendererPresentation`. Enable the existing
`TFL_BUILD_BACKEND_METAL` or `TFL_BUILD_BACKEND_VULKAN` option for the desired
platform. `TFL_BUILD_RENDERER_PRESENTATION` builds the optional native factory
and defaults to ON; linking only `Truffle::Renderer` does **not** link that
factory or any backend implementation. The install/export includes the
optional target only when enabled. Compiler toolchains are not required.

```cpp
#include <truffle/gui/painter.hpp>
#include <truffle/renderer/presentation.hpp>

// Host supplies an HWND on Windows, or a CAMetalLayer on macOS.
auto presentation = renderer::create_native_presentation({
    .native = native, .extent = drawablePixels,
});
// Check every Result/Status before accessing its value.
renderer::PipelineCache programs(presentation.value().device());
renderer::Renderer renderer(presentation.value().device(), &programs);
auto painter = gui::Painter::create(presentation.value().device(), programs, {
    .color_format = presentation.value().desc().format,
    .frames_in_flight = presentation.value().desc().framesInFlight,
});
```

Automatic setup chooses Metal for a `cocoa_layer` (Vulkan when Metal is not
built), Vulkan for `win32` or `xcb`. `NativePresentationOptions` accepts an
explicit backend, instance validation/diagnostics and device feature requests.
The selected factory's real errors are preserved; a failed Metal initialization
does not silently choose another device/backend. Disabled or other native
backends return `unsupported`, never Null. Metal/macOS borrows a CAMetalLayer;
Vulkan/Windows borrows an HWND and optional owning HINSTANCE in `display`;
Vulkan/Linux requires the existing XCB window/connection convention.
An SDL host obtains these native handles itself (and creates the Metal layer
when needed). Truffle does not include SDL or own its window.

See the compiled, host-independent
[`GuiApplication` example](../examples/gui_presentation/gui_application.hpp)
for a complete setup/draw/resize/error path without backend headers or RHI
initialization calls.

## One shared frame

1. Forward host input through `Context::handle`; report logical size and
   pixels-per-logical-unit with `Context::set_viewport`. Set `Presentation::resize`
   to the drawable **pixel** size, including zero while minimized.
2. When GUI/application work needs drawing, call `Presentation::begin_frame`.
   Its `PresentationFrame` reports the safe streamed-resource slot and extent.
3. Prepare GUI, record Painter batches with that slot/extent and append a
   swapchain `RenderPassNode` to the same graph as application/offscreen passes.
4. Call `Presentation::render(renderer, graph)`. It signals its FramePacer and
   closes the frame even on an error. Check its result and `last_frame_stats`.

The Renderer must reference the **same device object**. Pass custom shaders,
application data and dependencies through the existing PipelineCache and
FrameGraph; no fixed scene/camera/material model is imposed. Advanced hosts can
instead use `Presentation::create(existingDevice, desc)` to borrow their device,
or supply an owned `rhi::Instance` for device selection without the optional
factory. `device()` and `swapchain()` expose the existing lower-level contracts.
Submission outside this convenience is the caller's synchronization
responsibility; its idle wait covers only frames submitted through it.

## Suspension, recovery and lifetime

`begin_frame` reports `out_of_date` while either pixel dimension is zero. It
does not acquire an image, allocate a surface or consume a pacing slot. The host
sleeps until resize/restore rather than polling this status. Resize is rejected
while a frame is open; finish it or `cancel_frame` first.

Resize, `out_of_date`, `suboptimal` and failed recording/submission request
swapchain recreation before the next frame, after waiting for submitted work.
An acquired image abandoned by recording failure is retired with its old
swapchain. Application work is never automatically replayed. A suboptimal
acquisition is exposed even if presentation succeeds;
`RendererFrameStats::presentationSuboptimal` records it for explicit Renderer
clients too. `submitted` remains true when presentation later fails, so pacing
never overwrites in-use resources. `presented` includes suboptimal presentation.

`surface_lost` remains an explicit host-actionable error until
`replace_surface` lends a replacement handle. Keep the old handle alive until
that call succeeds. `device_lost` requires fresh device/presentation and all GPU
consumers; it is not disguised as a resize. Timeouts and backend errors retain
their typed Status/details. Before teardown call and check `wait_idle`, destroy
GPU users, then Presentation, then the host window. RHI retains submitted
native resources; Presentation destruction does not hide a failed idle wait.

GUI roots remain transparent. The application's render-pass clear color is
explicit (the example clears transparent); no background widget is required.

## Evidence boundary

Focused Null contracts cover ownership/moves, safe slots, resize/minimize,
invalid graphs/foreign devices, recording-failure acquisition retirement and
host surface replacement. The example is compile-checked. These are not native
pixel proofs. Existing native Metal/macOS and Vulkan/Windows implementations
are reused unchanged; new native execution and broader validation remain
owner-run work, not a backend-maturity promotion.

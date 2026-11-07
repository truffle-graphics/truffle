# GUI Rendering Viewport (#271/#277)

`gui::Viewport` is a checked reference to a Context-owned image/input element.
Its ordinary header imports GUI only. Application-owned scene rendering is an
explicit `gui/rendering.hpp` opt-in using Renderer types, never raw Painter or
RHI setup. The existing offscreen machinery remains underneath; there is no
Engine, scene schema, CPU readback or default direct-swapchain scene rendering.

## Layout and input

Include `truffle/gui/viewport.hpp`, create with
`Viewport::create(context, parent)`, set ordinary element layout and install
`set_event_handler(ViewportEventHandler)`. `metrics()` reports the last
completed layout's logical bounds, GUI `PixelSize`, host DPI scale and
drawable state. Pixel size is `ceil(full layout size * DPI)`, not the partially
clipped size; hidden, fully clipped or zero-sized surfaces report zero.
Detached `Surface` DPI remains element-specific.

`ViewportEvent` retains the actual `RoutedEvent` reference and optional local
logical/pixel coordinates for pointer/wheel input. Keyboard/focus/hover/
capture-loss use normal routed payloads. Normal clipping, topmost hit testing,
bubbling/defaults, focus and capture remain; the app chooses capture or
propagation policy. Captured positions can be outside the image. Handlers may
remove their own element safely.

`request_redraw()` increments the application-scene revision and requests a
GUI frame. It does not assume an Engine, camera or animation model.

## Owned and borrowed application integration

Both [Application paths](gui-application.md) use the same composition:

```cpp
#include <truffle/gui/rendering.hpp>

auto frame = gui::Rendering::begin_frame(app);
auto view = gui::Rendering::prepare_viewport(app, viewport, frame.value().slot);
renderer::FrameGraph graph;
if (view.value().redraw) {
    // App supplies Renderer batches, optional depth and clear/load/store.
    auto scene = gui::Rendering::append_viewport_pass(
        viewport, graph, sceneBatches, scenePass);
}
gui::Rendering::display_viewport(app, viewport);
app.context().update_shapes();
auto guiPass = gui::Rendering::append_gui_pass(app, graph, frame.value());
gui::Rendering::composite_viewport(viewport, graph, guiPass.value());
auto status = gui::Rendering::present(app, graph);
gui::Rendering::finish_viewport(viewport, app.last_frame_stats().submitted);
```

These abbreviated signatures are not error-handling examples. The compiled
[`draw_viewport` example](../examples/gui_viewport/viewport_frame.hpp) checks
every operation, finishes prepared viewports and cancels an abandoned frame.
Its callback receives `ViewportFrame` and the safe slot and returns application
batches/pass settings. GUI owns all GUI geometry, bindings and programs.
Use `Rendering::programs(app)` and `renderer(app)` for the combined frame;
an unrelated cache does not automatically contain the reserved GUI program.

`ViewportFrame::target` is a retained Renderer `Texture`, with size/format/
identity and a sampled view for normal program bindings. It pins the exact
target and device storage, not merely the viewport's mutable slot array.
`ViewportTargetDesc` uses Renderer format values; the default is BGRA8.
`frames_in_flight == 0` infers the Application's slot count; an explicit
different count fails. Release GPU state before changing device, format or
slot configuration.

`append_viewport_pass` supplies the offscreen color target/extent and the
sample-to-attachment transition. The app supplies clear/load/store, depth and
normal batches; another color target is rejected. `display_viewport` binds
the image before GUI recording without exposing Painter. Stable TextureId
rebinding preserves cached GUI shapes. Borders/tint/UVs, clipping, child
overlays and root transparency remain ordinary GUI behavior.
`composite_viewport` adds the scene-to-GUI graph dependency and
attachment-to-sample transition. Multiple viewports can contribute independent
scene passes before one GUI pass.

For explicit offscreen scheduling, `prepare_viewport(renderer::Device&, ...)`
uses a caller-protected slot; zero slot count defaults to two. The external
FramePacer, application streams and GUI stream must use matching safe slots.
`Rendering::record`/`append_gui_pass` accept a Surface belonging to the
Application's Context. An offscreen GUI pass can supply Renderer texture
transitions as the final `append_gui_pass` argument. A zero-pixel borrowed
Application can be explicitly recorded using a nonzero external target; this
does not create a window or make minimized `Application::draw` succeed.

One Application owns one Context and GUI stream. Surface DPI/input/recording
does not imply a complete shared-context, independently paced multi-window
Application API. Do not record two GUI views into one reused streamed slot:
each independently paced window needs an independent Painter/stream owner.
The normal borrowed-Context/per-surface ownership choice remains an explicit
follow-up boundary in #276; use specialist owners for that existing advanced
host arrangement rather than inventing a lifetime/pacing policy.

## Revision, synchronization and lifetime

First use, changed pixel size/DPI or an explicit scene revision redraws.
Unrelated GUI repaints, movement without size/DPI change and another safe slot
reuse the last **submitted** scene. Hidden/clipped/minimized views allocate no
target and preserve pending scene work. Each safe slot has a retained target;
resize replaces only that slot. Previously returned Texture values remain
valid across replacement and `release_viewport` while their owners are kept.

Pair every successful `prepare_viewport` with `finish_viewport`, including
hidden/cancelled work. Use actual submission statistics, not presentation
success: scene work can be submitted before the host presentation fails.
Unsubmitted work requests another frame. A submitted redraw without its GUI
transition is an explicit error.

Graph insertion retains the normal pass/batch/texture owners and GUI owner
needed by the underlying nodes. Graph moves preserve underlying identity.
Context owns only the element/raw viewport state; it does not strongly own
its enclosing Application through the normal wrapper. Native resources of
submitted commands follow RHI retention. Host windows/services and imported
raw devices remain externally owned and must stay alive/unmoved.

Finish/discard frames, check the relevant Presentation/FramePacer idle wait and
call `Rendering::release_viewport` before host/renderer transfer or teardown.
Stale references fail. Device loss requires new GPU consumers. Do not reuse
old graphs after slot reuse or texture/binding changes, and rebuild GUI,
Renderer and every consumer against matching headers/archives.

Explicit raw extensions now use `gui::specialist::Viewport` and
`gui/specialist/viewport.hpp`. This is a breaking pre-release cutover, not an
old-name alias or compatibility shim; no version bump is made.

## Focused evidence

`truffle_gui_only_consumer_tests` compiles/links GUI alone and exercises normal
viewport pixel metrics, input/focus, transparency and stale references.
`truffle_gui_viewport_consumer_tests` covers normal retained meshes/targets,
graph moves, submission/reuse/cancellation, resize and exact-target/device
lifetime after release/owner teardown, using Null for validation only.
The existing deep `truffle_gui_viewport_tests` preserves fractional/detached
DPI, capture, revision, barriers, hidden allocation, callback removal and
retention proofs through the specialist path.

All three focused CPU/Null contracts passed in the existing strict MSVC Debug
configuration. The migrated Vulkan native target compiled; it was not run.
Owner-run Metal/Vulkan native tests use the normal borrowed Application and
shared-frame APIs for four exact clip/overlay/scene-reuse GPU frames. Only
native initialization/readback remain specialist test harness code; BGRA
readback is normalized there, not in the public runtime.
Native owned/borrowed execution, Metal compilation, full suites, package/
configuration matrices and multi-window integration remain unverified.

# GUI Rendering Viewport (#271)

`gui::Viewport` is a checked reference to a Context-owned image/input element.
The application owns its scene, shaders, cameras, simulation and render data.
Renderer's existing `SampledTexture` creates offscreen targets; Painter samples
them in the GUI. There is no CPU readback in this path, no Engine/ECS dependency
and no default direct-to-swapchain scene rendering.

## Layout and input

```cpp
#include <truffle/gui/viewport.hpp>

auto viewport = gui::Viewport::create(context, parent);
// Check Result/Status values before using their contents.
context.set_layout_params(viewport.value().element(), {
    .width = 640, .height = 360, .weight = 1,
});
viewport.value().set_event_handler(
    [&](gui::Context& owner, gui::ViewportEvent& input) {
        if (const auto* pointer = input.routed.input<gui::PointerEvent>()) {
            // input.local: viewport-local logical coordinates.
            // input.pixels: viewport-local physical coordinates.
            if (pointer->action == gui::PointerAction::down) {
                owner.capture_pointer(viewport.value().element(), pointer->pointer);
            }
            // Update application state, then request a scene redraw when needed.
            viewport.value().request_redraw();
            input.routed.stop_propagation();
        }
    });
```

These abbreviated snippets show signatures; the compiled example checks every
operation. `metrics()` reports the last completed layout's surface-relative
logical bounds, rounded-up pixel size, host DPI scale and drawable state.
`dpi_scale` means pixels per logical unit, not a fabricated physical monitor
DPI. Size is the full laid-out image size, not the partially clipped rectangle.
It is zero when hidden, fully clipped or on a zero-sized surface.
`Context::scale(element)` follows an element's actual surface, including a
detached docking surface, rather than accidentally using the primary window's
DPI outside event dispatch.

Normal GUI topmost hit testing, bubbling, clipping, focus/Tab traversal and
capture are retained. The viewport is focusable; the application chooses when
to capture drags, stop bubbling or prevent defaults. Pointer/wheel events expose
local logical/pixel positions; keyboard, hover, focus and capture-loss
notifications use the existing routed payload. Captured positions may be
outside the viewport. Wheel deltas retain the normal GUI units/line convention.
Handlers are snapshotted so they may remove their own element safely.

## Render and composite in one frame

Use the device and safe slot from the same Presentation/FramePacer as Painter
and application per-frame data. After `Context::prepare_frame`:

```cpp
auto view = viewport.prepare_frame(device, slot, {
    .format = rhi::TextureFormat::bgra8_unorm,
    .frames_in_flight = framesInFlight,
});
if (view.value().redraw) {
    // App builds arbitrary RenderBatch data/shaders and optional depth.
    // view.target and view.metrics.pixels describe the offscreen destination.
    auto appPass = viewport.append_render_pass(graph, appBatches, appPassSettings);
}
viewport.display(painter);
context.update_shapes(); // only new/changed image decorations
auto batches = painter.record(context, slot, windowPixels);
auto guiPass = graph.add_node(std::make_unique<renderer::RenderPassNode>(
    windowPassSettings, std::move(batches).value(), true));
viewport.composite(graph, guiPass);
auto status = presentation.render(renderer, graph);
viewport.finish_frame(presentation.last_frame_stats().submitted);
```

The complete error-aware
[`draw_viewport` example](../examples/gui_viewport/viewport_frame.hpp) composes
the [GUI-only presentation example](../examples/gui_presentation/gui_application.hpp).
Its render callback supplies application batches and render-pass settings,
including optional depth. `GuiApplication` exposes its Painter, Renderer and
PipelineCache so GUI-only and custom rendering share the same resources.

`append_render_pass` supplies the offscreen color attachment/extent and an
explicit shader-read-to-color-attachment barrier. The app supplies clear,
load/store, depth and batch content; no scene schema is assumed. `composite`
adds the application-to-GUI dependency and the color-attachment-to-shader-read
barrier to that GUI node. Resource-usage ordering alone would not insert this
transition. Multiple viewports can append independent passes to one graph.
Advanced applications retain the target, node ID and full FrameGraph API to
compose other compute/render dependencies.

For detached windows, call `Context::prepare_frame` once for all views and use
that view's `Surface::record` for its GUI batches, then composite into its GUI
pass. Each independently paced Presentation needs its own Painter and
slot-scoped application uploads: two independent pacing slots do not protect
shared streamed buffers. The device/cache/Renderer may still be shared.

`display` registers/rebinds the prepared target with the same-device Painter.
`Painter::replace_texture` keeps its TextureId stable, so a scene-only redraw
or target-slot change does not regenerate retained GUI shapes. It creates the
new binding before replacing the old one and propagates errors. Viewport
decorations retain ordinary tint, borders, corners and UVs; children/overlays
remain ordinary GUI elements. Root transparency is unchanged. The application
chooses its window-pass clear color; no special background widget is needed.

## Redraw, resize and lifetime

`request_redraw()` increments the scene revision and requests a GUI frame.
Application animations schedule those requests through the existing host loop
or Context deadlines. An unrelated GUI repaint, movement without size change
or another pacing slot reuses the last **submitted** scene target. First use,
pixel-size or DPI change and an explicit scene revision require rendering.
Hidden/fully clipped/minimized views allocate no target and leave the scene
revision pending until drawable again.

Each safe pacing slot has a stable sampled target for scene redraws. Resize
replaces that slot's target and retains the previously submitted/displayed
image until it is replaced. GUI-only frames can sample the last committed
target across slots; all reads/writes stay on the same graphics queue, ordered
with explicit barriers. Offscreen initialization uses the existing bounded
Renderer layout-transition submission, never texture readback.

Pair every successful `prepare_frame` with `finish_frame`, even for hidden or
cancelled work. Pass `RendererFrameStats::submitted`: presentation may fail
after scene work was already submitted. Unsubmitted work remains dirty and
requests another frame. Submitting a redraw without its composite transition
is an explicit error. A frame graph is per-frame; after rendering/discarding
it, do not resubmit it after texture rebinding or streamed-slot reuse.

FrameGraph's narrow `retain(shared_ptr)` hook preserves the viewport state and
target wrappers borrowed by its nodes/batches, even when the element/reference
is removed after assembly. RHI retains the native resources of submitted
commands. The device, Painter and cache stay alive and unmoved through their
users and outstanding graphs; a Painter registration is private to the
viewport and must not be removed independently.

FrameGraph storage and RendererFrameStats changed in this unreleased batch.
Rebuild Renderer, GUI and every consumer against matching headers/archives;
do not combine new headers with Studio or other binaries built against the old
class layouts. This follows the pre-first-release policy, with no shim or
version bump.

Before destroying/moving a Painter/device or transferring to another host's
Painter, finish/discard outstanding frames, check the presentation/pacer idle
wait and call `release_gpu()`. This removes the registration and clears the
targets; the next prepared drawable frame renders again. Element/Context
cleanup retires registrations automatically, deferring them through an
outstanding graph. Stale object calls report an error. A lost device requires
new GPU consumers, not reuse of old targets; all allocation, binding, recording
and synchronization errors propagate as typed Status/Result values.

## Focused contract evidence

`truffle_gui_viewport_tests` covers exact fractional pixel/DPI sizing, detached
surface DPI, routed local input/focus/capture, scene revision/cancellation/reuse,
target resize and graph barriers/dependencies, invalid configuration, hidden/
zero-sized views, callback removal, stale references and graph retention using
the strict Null backend. It also compiles the public shared-frame example.

Owner-run `truffle_gui_viewport_metal_tests` and
`truffle_gui_viewport_vulkan_tests` exercise four real shared GPU frames and
exact offscreen-image/ancestor-clip/overlay pixels, stable TextureId rebinding
and zero scene-only shape regeneration. These tests do not add readback to
the public runtime path. Unrun native tests are not evidence or a platform
promotion; the existing Metal/macOS and Vulkan/Windows Painter paths are
preserved without a new compiler or dependency.

This delivery ran only the two new Null/CPU presentation and viewport contracts
in the existing strict Debug configuration. The new Vulkan viewport test also
compiled on Windows; neither native viewport test nor native Presentation
execution was run. Native runtime, multi-window integration and broader
verification remain owner-run.

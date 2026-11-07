#include "../examples/gui_viewport/viewport_frame.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/draw.hpp"
#include "truffle/gui/surface.hpp"
#include "truffle/gui/viewport.hpp"
#include "truffle/renderer/specialist/frame_stream.hpp"

#include <limits>

namespace {

using namespace truffle;
using Clock = gui::Context::Clock;

void place(gui::Context &context, gui::ElementId element, gui::Rect bounds) {
  tests::require(context.set_layout_params(element, {
                                                        .width = bounds.width,
                                                        .height = bounds.height,
                                                        .position = {bounds.x, bounds.y},
                                                    }));
}

renderer::specialist::FrameGraph::NodeId gui_pass(renderer::specialist::FrameGraph &graph, rhi::Texture &color) {
  return graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
      rhi::RenderPassDesc{.extent = {400, 240},
                          .colorAttachments = {{.texture = &color}}},
      std::vector<renderer::specialist::RenderBatch>{}));
}

} // namespace

int main() {
  gui::Viewport invalid;
  TFL_CHECK(!invalid.alive());
  TFL_CHECK(!invalid.metrics().ok());
  TFL_CHECK(!invalid.request_redraw().ok());
  gui::Context context;
  const auto now = Clock::now();
  TFL_CHECK(context.set_viewport({200, 120}, 2).ok());
  auto outer = context.create(context.root());
  TFL_CHECK(outer.ok());
  place(context, outer.value(), {10, 5, 80, 60});
  auto result = gui::Viewport::create(context, outer.value());
  TFL_CHECK(result.ok());
  auto viewport = result.value();
  place(context, viewport.element(), {5, 3, 40.25F, 20.25F});
  TFL_CHECK(context.prepare_frame(now).status.ok());
  const auto metrics = viewport.metrics();
  TFL_CHECK(metrics.ok() && metrics.value().drawable);
  TFL_CHECK((metrics.value().bounds == gui::Rect{15, 8, 40.25F, 20.25F}));
  TFL_CHECK((metrics.value().pixels == rhi::Extent2D{81, 41}));
  TFL_CHECK(metrics.value().dpi_scale == 2);
  TFL_CHECK(context.decoration(context.root()).value().fill.a == 0);

  // Ordinary routed input, local coordinates, focus and pointer capture.
  int pointerEvents = 0;
  int keyEvents = 0;
  int captureLost = 0;
  bool inputCorrect = true;
  TFL_CHECK(viewport
                .set_event_handler([&](gui::Context &owner, gui::ViewportEvent &event) {
                  if (const auto *pointer = event.routed.input<gui::PointerEvent>()) {
                    ++pointerEvents;
                    if (pointer->action == gui::PointerAction::down) {
                      inputCorrect = inputCorrect && event.local == gui::Point{4, 6} &&
                                     event.pixels == gui::Point{8, 12};
                      tests::require(
                          owner.capture_pointer(viewport.element(), pointer->pointer));
                    }
                    event.routed.stop_propagation();
                  }
                  if (event.routed.input<gui::KeyEvent>()) {
                    ++keyEvents;
                    inputCorrect = inputCorrect && !event.local && !event.pixels;
                  }
                  if (std::get_if<gui::CaptureLostEvent>(&event.routed.payload)) {
                    ++captureLost;
                  }
                })
                .ok());
  TFL_CHECK(context
                .handle(gui::PointerEvent{
                    .action = gui::PointerAction::down,
                    .position = {19, 14},
                    .button = gui::PointerButton::primary,
                })
                .ok());
  TFL_CHECK(context.focused() == viewport.element());
  TFL_CHECK(context.handle(gui::PointerEvent{.position = {199, 119}}).ok());
  TFL_CHECK(context.handle(gui::KeyEvent{.key = gui::Key::a}).ok());
  TFL_CHECK(context
                .handle(gui::PointerEvent{
                    .action = gui::PointerAction::up,
                    .position = {199, 119},
                    .button = gui::PointerButton::primary,
                })
                .ok());
  TFL_CHECK(pointerEvents == 3 && keyEvents == 1 && captureLost == 1);
  TFL_CHECK(inputCorrect);
  TFL_CHECK(!context.captured().valid());

  // Per-element DPI follows detached surfaces even outside their dispatch.
  auto detached = gui::Surface::create(context);
  TFL_CHECK(detached.ok());
  TFL_CHECK(detached.value()->set_viewport({100, 70}, 1.25F).ok());
  TFL_CHECK(detached.value()->attach(outer.value()).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(viewport.metrics().value().dpi_scale == 1.25F);
  TFL_CHECK((viewport.metrics().value().pixels == rhi::Extent2D{51, 26}));
  TFL_CHECK(context.scale(context.root()).value() == 2);
  auto primary = context.create(context.root());
  TFL_CHECK(primary.ok());
  bool dpiCorrect = false;
  TFL_CHECK(viewport
                .set_event_handler([&](gui::Context &owner, gui::ViewportEvent &) {
                  dpiCorrect =
                      owner.scale() == 1.25F && owner.scale(primary.value()).value() == 2;
                })
                .ok());
  TFL_CHECK(detached.value()->handle(gui::PointerEvent{.position = {19, 14}}).ok());
  TFL_CHECK(dpiCorrect);
  TFL_CHECK(viewport.set_event_handler({}).ok());
  TFL_CHECK(context.reparent(outer.value(), context.root()).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(viewport.metrics().value().dpi_scale == 2);

  auto gpu = tests::make_null_context();
  auto &device = gpu.device;
  renderer::specialist::Renderer renderer(device);
  auto pacerResult = renderer::specialist::FramePacer::create(device);
  TFL_CHECK(pacerResult.ok());
  auto pacer = std::move(pacerResult).value();
  auto output = device.create_texture({
      .extent = {400, 240, 1},
      .usage = rhi::TextureUsage::color_attachment,
  });
  TFL_CHECK(output.ok());
  TFL_CHECK(!viewport.prepare_frame(device, 2).ok());
  TFL_CHECK(!viewport.prepare_frame(device, 0, {.frames_in_flight = 0}).ok());
  TFL_CHECK(!viewport.finish_frame(false).ok());

  auto slot = pacer.begin_frame();
  TFL_CHECK(slot.ok());
  auto frame = viewport.prepare_frame(device, slot.value());
  TFL_CHECK(frame.ok() && frame.value().redraw && frame.value().target != nullptr);
  auto *firstTarget = frame.value().target;
  TFL_CHECK(!viewport.prepare_frame(device, slot.value()).ok());
  TFL_CHECK(!viewport.release_gpu().ok());
  renderer::specialist::FrameGraph graph;
  TFL_CHECK(!viewport
                 .append_render_pass(graph, {},
                                     {.colorAttachments = {{.texture = &output.value()}}})
                 .ok());
  auto appPass = viewport.append_render_pass(graph, {});
  TFL_CHECK(appPass.ok());
  const auto *app =
      static_cast<const renderer::specialist::RenderPassNode *>(graph.node(appPass.value()));
  TFL_CHECK(app->explicit_desc()->colorAttachments.front().texture ==
            &firstTarget->texture());
  TFL_CHECK(app->barriers_before().front().oldLayout ==
            rhi::TextureLayout::shader_read_only);
  renderer::specialist::FrameGraph foreignGraph;
  TFL_CHECK(
      !viewport.composite(foreignGraph, gui_pass(foreignGraph, output.value())).ok());
  TFL_CHECK(!viewport.composite(graph, appPass.value()).ok());
  const auto composite = gui_pass(graph, output.value());
  TFL_CHECK(viewport.composite(graph, composite).ok());
  TFL_CHECK(!viewport.composite(graph, composite).ok());
  const auto *guiNode =
      static_cast<const renderer::specialist::RenderPassNode *>(graph.node(composite));
  TFL_CHECK(guiNode->barriers_before().front().newLayout ==
            rhi::TextureLayout::shader_read_only);
  TFL_CHECK(graph.dependencies().size() == 1);
  TFL_CHECK(renderer.render(graph, nullptr, pacer.signal()).ok());
  pacer.end_frame(renderer.last_frame_stats().submitted);
  TFL_CHECK(viewport.finish_frame(renderer.last_frame_stats().submitted).ok());

  // GUI-only repaints reuse the last submitted scene, including another slot.
  TFL_CHECK(context.prepare_frame(now).status.ok());
  slot = pacer.begin_frame();
  TFL_CHECK(slot.ok());
  auto retained = viewport.prepare_frame(device, slot.value());
  TFL_CHECK(retained.ok() && !retained.value().redraw &&
            retained.value().target == firstTarget);
  TFL_CHECK(!viewport.append_render_pass(graph, {}).ok());
  TFL_CHECK(viewport.finish_frame(false).ok());
  pacer.end_frame(false);

  // Cancelled work stays dirty; submitted scene work commits independently of
  // whether the host later succeeds at presenting the complete frame.
  TFL_CHECK(viewport.request_redraw().ok());
  TFL_CHECK(context.needs_frame(now));
  TFL_CHECK(context.prepare_frame(now).status.ok());
  auto cancelled = viewport.prepare_frame(device, 0);
  TFL_CHECK(cancelled.ok() && cancelled.value().redraw);
  TFL_CHECK(viewport.finish_frame(false).ok());
  TFL_CHECK(context.needs_frame(now));
  auto retry = viewport.prepare_frame(device, 1);
  TFL_CHECK(retry.ok() && retry.value().redraw);
  renderer::specialist::FrameGraph retryGraph;
  TFL_CHECK(viewport.append_render_pass(retryGraph, {}).ok());
  TFL_CHECK(viewport.composite(retryGraph, gui_pass(retryGraph, output.value())).ok());
  TFL_CHECK(renderer.render(retryGraph).ok());
  TFL_CHECK(viewport.finish_frame(true).ok());
  auto unchanged = viewport.prepare_frame(device, 0);
  TFL_CHECK(unchanged.ok() && !unchanged.value().redraw);
  TFL_CHECK(viewport.finish_frame(false).ok());

  // DPI changes request a scene redraw even when rounded pixels stay the same.
  TFL_CHECK(context.set_viewport({200, 120}, 2.0001F).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  auto dpiChanged = viewport.prepare_frame(device, 0);
  TFL_CHECK(dpiChanged.ok() && dpiChanged.value().redraw &&
            dpiChanged.value().metrics.pixels == metrics.value().pixels);
  TFL_CHECK(viewport.finish_frame(false).ok());
  TFL_CHECK(context.set_viewport({200, 120}, 2).ok());

  // Moving needs only GUI composition; pixel/DPI changes require a new target.
  place(context, viewport.element(), {6, 4, 40.25F, 20.25F});
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(!viewport.prepare_frame(device, 0).value().redraw);
  TFL_CHECK(viewport.finish_frame(false).ok());
  place(context, viewport.element(), {6, 4, 50, 30});
  TFL_CHECK(context.prepare_frame(now).status.ok());
  auto resized = viewport.prepare_frame(device, 0);
  TFL_CHECK(resized.ok() && resized.value().redraw);
  TFL_CHECK(
      (resized.value().target->texture().desc().extent == rhi::Extent3D{100, 60, 1}));
  TFL_CHECK(viewport.finish_frame(false).ok());
  TFL_CHECK(!viewport.prepare_frame(gpu.device, 0, {.frames_in_flight = 3}).ok());
  auto other = tests::make_null_context();
  TFL_CHECK(!viewport.prepare_frame(other.device, 0).ok());

  TFL_CHECK(context
                .set_placement(viewport.element(),
                               gui::Rect{0, 0, std::numeric_limits<float>::max(), 30})
                .ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(viewport.metrics().status().code == base::StatusCode::invalid_argument);
  TFL_CHECK(!viewport.prepare_frame(device, 0).ok());
  TFL_CHECK(context.set_placement(viewport.element(), {}).ok());

  // Collapsed ancestors, total clipping and zero host size allocate nothing.
  TFL_CHECK(context.set_visible(outer.value(), false).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  const auto textures = gpu.instance.stats().texturesCreated;
  auto hidden = viewport.prepare_frame(device, 0);
  TFL_CHECK(hidden.ok() && !hidden.value().metrics.drawable &&
            hidden.value().target == nullptr);
  TFL_CHECK(viewport.finish_frame(false).ok());
  TFL_CHECK(gpu.instance.stats().texturesCreated == textures);
  TFL_CHECK(context.set_visible(outer.value(), true).ok());
  place(context, viewport.element(), {90, 70, 40, 20});
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(!viewport.metrics().value().drawable);
  TFL_CHECK(context.set_overlay(viewport.element(), true).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(viewport.metrics().value().drawable);
  TFL_CHECK(context.set_viewport({0, 0}, 2).ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  auto minimized = viewport.prepare_frame(device, 0);
  TFL_CHECK(minimized.ok() && !minimized.value().metrics.drawable);
  TFL_CHECK(viewport.finish_frame(false).ok());
  TFL_CHECK(gpu.instance.stats().texturesCreated == textures);

  // Graph retention protects targets if the element and references are
  // removed after graph assembly, before the renderer consumes them.
  TFL_CHECK(context.set_viewport({200, 120}, 2).ok());
  TFL_CHECK(context.set_overlay(viewport.element(), false).ok());
  place(context, viewport.element(), {5, 3, 40, 20});
  TFL_CHECK(context.prepare_frame(now).status.ok());
  auto outstanding = viewport.prepare_frame(device, 0);
  TFL_CHECK(outstanding.ok() && outstanding.value().redraw);
  renderer::specialist::FrameGraph lastGraph;
  TFL_CHECK(viewport.append_render_pass(lastGraph, {}).ok());
  TFL_CHECK(viewport.composite(lastGraph, gui_pass(lastGraph, output.value())).ok());
  TFL_CHECK(context.destroy(outer.value()).ok());
  TFL_CHECK(!viewport.alive() && !viewport.metrics().ok());
  viewport = {};
  result = base::Status::failure(base::StatusCode::invalid_state, "drop reference");
  TFL_CHECK(renderer.render(lastGraph).ok());
  lastGraph = {};
  TFL_CHECK(pacer.wait_idle().ok());

  gui::specialist::Painter emptyPainter;
  TFL_CHECK(!emptyPainter.valid());
  TFL_CHECK(!emptyPainter.remove_texture({1}).ok());
  renderer::specialist::SampledTexture emptyTexture;
  TFL_CHECK(!emptyPainter.replace_texture({1}, emptyTexture).ok());

  // A callback may destroy the element that owns the handler.
  auto removed = gui::Viewport::create(context, context.root());
  TFL_CHECK(removed.ok());
  place(context, removed.value().element(), {0, 0, 20, 20});
  TFL_CHECK(removed.value()
                .set_event_handler([&](gui::Context &owner, gui::ViewportEvent &event) {
                  if (event.routed.input<gui::PointerEvent>()) {
                    tests::require(owner.destroy(removed.value().element()));
                  }
                })
                .ok());
  TFL_CHECK(context.prepare_frame(now).status.ok());
  TFL_CHECK(context.handle(gui::PointerEvent{.position = {2, 2}}).ok());
  TFL_CHECK(!removed.value().alive());

  // Context teardown leaves checked references safely stale.
  gui::Viewport stale;
  {
    gui::Context owner;
    auto reference = gui::Viewport::create(owner, owner.root());
    TFL_CHECK(reference.ok());
    stale = reference.value();
  }
  TFL_CHECK(!stale.alive() && !stale.request_redraw().ok());
}

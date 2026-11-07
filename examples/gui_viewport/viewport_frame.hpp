#pragma once

#include "truffle/gui/rendering.hpp"

namespace truffle::examples {

struct ViewportRenderData {
  std::vector<renderer::RenderBatch> batches;
  renderer::RenderPassDesc pass; // app-owned depth/clear settings
};
using RenderViewport = std::function<base::Result<ViewportRenderData>(
    const gui::ViewportFrame &, std::uint32_t slot)>;

// Graphics owns its rendering. The app renders only its scene and supplies
// normal Renderer batches/programs; Context remains application-owned.
inline base::Status draw_viewport(gui::Graphics &graphics, gui::Context &context,
                                  gui::Viewport &viewport,
                                  const RenderViewport &render_viewport,
                                  gui::Context::Clock::time_point now) {
  auto frame = gui::Rendering::begin_frame(graphics, context, now);
  if (!frame.ok())
    return frame.status();
  bool prepared = false;
  auto fail = [&](base::Status status) {
    if (prepared) {
      auto finished = gui::Rendering::finish_viewport(viewport, false);
      if (!finished.ok())
        status = finished;
    }
    auto cancelled = gui::Rendering::cancel_frame(graphics, context);
    return cancelled.ok() ? status : cancelled;
  };
  auto view =
      gui::Rendering::prepare_viewport(graphics, context, viewport, frame.value().slot);
  if (!view.ok())
    return fail(view.status());
  prepared = true;
  renderer::FrameGraph graph;
  if (view.value().redraw) {
    if (!render_viewport)
      return fail(base::Status::failure(base::StatusCode::invalid_argument,
                                        "Viewport requires an application renderer"));
    auto scene = render_viewport(view.value(), frame.value().slot);
    if (!scene.ok())
      return fail(scene.status());
    auto pass = gui::Rendering::append_viewport_pass(
        viewport, graph, std::move(scene.value().batches), std::move(scene.value().pass));
    if (!pass.ok())
      return fail(pass.status());
  }
  if (auto status =
          gui::Rendering::display_viewport(graphics, context, viewport);
      !status.ok())
    return fail(status);
  context.update_shapes();
  auto gui_pass =
      gui::Rendering::append_gui_pass(graphics, context, graph, frame.value(),
                                      {
                                          .colorAttachments = {{.clear = {0, 0, 0, 0}}},
                                      });
  if (!gui_pass.ok())
    return fail(gui_pass.status());
  if (auto status = gui::Rendering::composite_viewport(viewport, graph, gui_pass.value());
      !status.ok())
    return fail(status);
  auto status = gui::Rendering::present(graphics, context, graph);
  auto finished =
      gui::Rendering::finish_viewport(viewport, graphics.last_frame_stats().submitted);
  return status.ok() ? finished : status;
}

} // namespace truffle::examples

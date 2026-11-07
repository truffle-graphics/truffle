#pragma once

#include "truffle/gui/viewport.hpp"
#include "truffle/renderer/specialist/presentation.hpp"

namespace truffle::examples {

struct ViewportRenderData {
  std::vector<renderer::specialist::RenderBatch> batches;
  rhi::RenderPassDesc pass; // optional application-owned depth and clear settings
};

using RenderViewport = std::function<base::Result<ViewportRenderData>(
    const gui::ViewportFrame &, std::uint32_t slot)>;

// The host creates its window and Presentation/Painter as in gui_presentation.
// Scene state, shaders and per-slot geometry/uniforms belong to renderViewport.
inline base::Status draw_viewport(renderer::specialist::Presentation &presentation,
                                  renderer::specialist::Renderer &renderer, gui::Context &context,
                                  gui::Painter &painter, gui::Viewport &viewport,
                                  const RenderViewport &renderViewport,
                                  gui::Context::Clock::time_point now) {
  auto frame = presentation.begin_frame();
  if (!frame.ok()) {
    return frame.status();
  }
  bool preparedViewport = false;
  auto fail = [&](base::Status status) {
    if (preparedViewport) {
      (void)viewport.finish_frame(false);
    }
    (void)presentation.cancel_frame();
    context.request_frame();
    return status;
  };
  auto prepared = context.prepare_frame(now);
  if (!prepared.status.ok()) {
    return fail(prepared.status);
  }
  auto view =
      viewport.prepare_frame(presentation.device(), frame.value().slot,
                             {
                                 .frames_in_flight = presentation.desc().framesInFlight,
                             });
  if (!view.ok()) {
    return fail(view.status());
  }
  preparedViewport = true;
  renderer::specialist::FrameGraph graph;
  if (view.value().redraw) {
    if (!renderViewport) {
      return fail(
          base::Status::failure(base::StatusCode::invalid_argument,
                                "Viewport example needs an application renderer"));
    }
    auto rendered = renderViewport(view.value(), frame.value().slot);
    if (!rendered.ok()) {
      return fail(rendered.status());
    }
    auto node = viewport.append_render_pass(graph, std::move(rendered.value().batches),
                                            std::move(rendered.value().pass));
    if (!node.ok()) {
      return fail(node.status());
    }
  }
  if (auto status = viewport.display(painter); !status.ok()) {
    return fail(status);
  }
  context.update_shapes(); // image registration only; layout was already prepared
  auto batches = painter.record(context, frame.value().slot, frame.value().extent);
  if (!batches.ok()) {
    return fail(batches.status());
  }
  rhi::RenderPassDesc pass;
  pass.colorAttachments.push_back({.clear = {0, 0, 0, 0}});
  const auto guiPass = graph.add_node(
      std::make_unique<renderer::specialist::RenderPassNode>(pass, std::move(batches).value(), true));
  if (auto status = viewport.composite(graph, guiPass); !status.ok()) {
    return fail(status);
  }
  auto status = presentation.render(renderer, graph);
  const auto finished = viewport.finish_frame(presentation.last_frame_stats().submitted);
  if (!status.ok() || !finished.ok()) {
    context.request_frame();
  }
  return status.ok() ? finished : status;
}

} // namespace truffle::examples

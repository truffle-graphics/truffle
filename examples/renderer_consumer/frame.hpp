#pragma once

#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/presentation.hpp"
#include "truffle/renderer/view.hpp"

namespace truffle::examples {

// No backend or RHI types. The app may create Device/Presentation through the
// optional native factory, or obtain the shared Renderer device from GUI.
inline base::Status render_mesh(renderer::Presentation &presentation,
                                renderer::Renderer &renderer, const renderer::Mesh &mesh,
                                renderer::MaterialId program,
                                renderer::ClearColor clear = {}) {
  auto frame = presentation.begin_frame();
  if (!frame.ok())
    return frame.status();
  auto batch = mesh.batch(program);
  if (!batch.ok()) {
    (void)presentation.cancel_frame();
    return batch.status();
  }
  renderer::FrameGraph graph;
  auto node = graph.add_render_pass({
      .pass = {.colorAttachments = {{.clear = clear}}},
      .batches = {std::move(batch).value()},
      .useWindow = true,
  });
  if (!node.ok()) {
    (void)presentation.cancel_frame();
    return node.status();
  }
  return presentation.render(renderer, graph);
}

} // namespace truffle::examples

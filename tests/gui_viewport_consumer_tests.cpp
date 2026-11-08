#include "../examples/gui_viewport/viewport_frame.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/rhi_interop.hpp" // validation-device setup only

#include <array>

namespace {
using namespace truffle;

int run(renderer::Device &device, renderer::Texture &retained) {
  gui::Context context;
  TFL_CHECK(context.set_viewport({32, 24}).ok());
  auto viewport = gui::Viewport::create(context, context.root());
  TFL_CHECK(viewport.ok());
  auto &view = viewport.value();
  TFL_CHECK(context
                .set_layout_params(view.element(),
                                   {
                                       .width = 16.0F,
                                       .height = 10.0F,
                                       .position = {2, 2},
                                   })
                .ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
  renderer::Device invalid;
  TFL_CHECK(!gui::Rendering::prepare_viewport(invalid, view, 0).ok());
  TFL_CHECK(!gui::Rendering::prepare_viewport(device, view, 2).ok());
  TFL_CHECK(!gui::Rendering::prepare_viewport(
                 device, view, 0,
                 {
                     .format = static_cast<renderer::TextureFormat>(999),
                 })
                 .ok());
  auto pacer = renderer::FramePacer::create(device);
  TFL_CHECK(pacer.ok());
  renderer::Renderer render(device);
  auto output = renderer::SampledTexture::render_target(device, {.extent = {32, 24}});
  TFL_CHECK(output.ok());
  {
    auto slot = pacer.value().begin_frame();
    TFL_CHECK(slot.ok());
    auto frame = gui::Rendering::prepare_viewport(device, view, slot.value());
    TFL_CHECK(frame.ok() && frame.value().redraw && frame.value().target.valid());
    retained = frame.value().target;
    renderer::FrameGraph graph;
    {
      const std::array<float, 6> vertices{-1, -1, 1, -1, 0, 1};
      const std::array<renderer::MeshStream, 1> streams{{
          {std::as_bytes(std::span{vertices}), 2 * sizeof(float)},
      }};
      auto mesh = renderer::Mesh::upload(device, {.streams = streams, .vertexCount = 3});
      TFL_CHECK(mesh.ok());
      auto batch = mesh.value().batch(7);
      TFL_CHECK(batch.ok());
      TFL_CHECK(gui::Rendering::append_viewport_pass(view, graph,
                                                     {
                                                         std::move(batch).value(),
                                                     })
                    .ok());
    }
    auto gui_pass = graph.add_render_pass({
        .pass = {.extent = {32, 24},
                 .colorAttachments = {{.texture = output.value().texture()}}},
        .transitions = {output.value().to_attachment()},
    });
    TFL_CHECK(gui_pass.ok());
    auto moved = std::move(graph);
    TFL_CHECK(!gui::Rendering::composite_viewport(view, graph, gui_pass.value()).ok());
    TFL_CHECK(gui::Rendering::composite_viewport(view, moved, gui_pass.value()).ok());
    TFL_CHECK(render.render(moved, pacer.value().signal()).ok());
    pacer.value().end_frame(render.last_frame_stats().submitted);
    TFL_CHECK(
        gui::Rendering::finish_viewport(view, render.last_frame_stats().submitted).ok());
    TFL_CHECK(pacer.value().wait_idle().ok());
  }
  auto slot = pacer.value().begin_frame();
  TFL_CHECK(slot.ok());
  auto unchanged = gui::Rendering::prepare_viewport(device, view, slot.value());
  TFL_CHECK(unchanged.ok() && !unchanged.value().redraw);
  TFL_CHECK(unchanged.value().target.id() == retained.id());
  TFL_CHECK(gui::Rendering::finish_viewport(view, false).ok());
  pacer.value().end_frame(false);
  TFL_CHECK(context
                .set_layout_params(view.element(),
                                   {
                                       .width = 20.0F,
                                       .height = 12.0F,
                                       .position = {2, 2},
                                   })
                .ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
  auto resized = gui::Rendering::prepare_viewport(device, view, 0);
  TFL_CHECK(resized.ok() && resized.value().redraw);
  TFL_CHECK((resized.value().target.extent() == renderer::PixelSize{20, 12}));
  TFL_CHECK(resized.value().target.id() != retained.id());
  TFL_CHECK((retained.extent() == renderer::PixelSize{16, 10}));
  TFL_CHECK(gui::Rendering::finish_viewport(view, false).ok());
  TFL_CHECK(context.set_viewport({0, 0}).ok());
  TFL_CHECK(context.prepare_frame(gui::Context::Clock::now()).status.ok());
  auto minimized = gui::Rendering::prepare_viewport(device, view, 0);
  TFL_CHECK(minimized.ok() && !minimized.value().metrics.drawable &&
            !minimized.value().redraw && !minimized.value().target.valid());
  TFL_CHECK(gui::Rendering::finish_viewport(view, false).ok());
  TFL_CHECK(gui::Rendering::release_viewport(view).ok());
  TFL_CHECK(retained.valid() &&
            retained.format() == renderer::TextureFormat::bgra8_unorm);
  return 0;
}
} // namespace

int main() {
  using namespace truffle;
  renderer::Texture retained;
  {
    auto gpu = tests::make_null_context();
    auto owned =
        renderer::RhiInterop::adopt(std::move(gpu.instance), std::move(gpu.device));
    TFL_CHECK(owned.ok());
    TFL_CHECK(run(owned.value(), retained) == 0);
  }
  TFL_CHECK(retained.valid());
  TFL_CHECK((retained.extent() == renderer::PixelSize{16, 10}));
}

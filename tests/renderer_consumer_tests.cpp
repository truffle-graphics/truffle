#include "../examples/renderer_consumer/frame.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/rhi_interop.hpp" // validation harness only

#include <array>

int main() {
  using namespace truffle;
  auto gpu = tests::make_null_context();
  auto imported = renderer::RhiInterop::borrow(gpu.device);
  TFL_CHECK(imported.ok());
  auto device = std::move(imported).value();
  std::array<float, 6> positions{-1, -1, 1, -1, 0, 1};
  const std::array<renderer::MeshStream, 1> streams{{
      {std::as_bytes(std::span{positions}), 2 * sizeof(float)},
  }};
  auto mesh = renderer::Mesh::upload(device, {.streams = streams, .vertexCount = 3});
  TFL_CHECK(mesh.ok());
  auto batch = mesh.value().batch(7);
  TFL_CHECK(batch.ok() && batch.value().bindings[0].valid());
  auto buffer = renderer::Buffer::upload(device, std::as_bytes(std::span{positions}),
                                         renderer::BufferUsage::vertex);
  TFL_CHECK(buffer.ok() && buffer.value().slice().ok());
  TFL_CHECK(!buffer.value().slice(1000).ok());
  auto target = renderer::SampledTexture::render_target(device, {.extent = {16, 16}});
  TFL_CHECK(target.ok());
  TFL_CHECK((target.value().texture().extent() == renderer::PixelSize{16, 16}));
  auto layout = renderer::ProgramLayout::sampled_texture(device);
  TFL_CHECK(layout.ok());
  const std::array<renderer::BindingValue, 2> values{{
      {.binding = 0, .texture = target.value().texture()},
      {.binding = 1},
  }};
  auto bindings = layout.value().bind(device, 0, values);
  TFL_CHECK(bindings.ok() && bindings.value().valid());
  renderer::FrameGraph graph;
  auto pass = graph.add_render_pass({
      .pass = {.extent = {16, 16},
               .colorAttachments = {{.texture = target.value().texture()}}},
      .batches = {std::move(batch).value()},
      .transitions = {target.value().to_attachment()},
  });
  TFL_CHECK(pass.ok());
  auto after = graph.add_render_pass({
      .pass = {.extent = {16, 16}},
      .transitions = {target.value().to_shader_read()},
  });
  TFL_CHECK(after.ok() && graph.add_dependency(pass.value(), after.value()).ok());
  renderer::Renderer renderer(device);
  auto pacer = renderer::FramePacer::create(device);
  TFL_CHECK(pacer.ok() && pacer.value().begin_frame().ok());
  TFL_CHECK(renderer.render(graph, pacer.value().signal()).ok());
  pacer.value().end_frame(renderer.last_frame_stats().submitted);
  TFL_CHECK(pacer.value().wait_idle().ok());
  TFL_CHECK(renderer.last_frame_stats().renderBatchesExecuted == 1);
  TFL_CHECK(graph.add_dependency(after.value(), pass.value()).ok());
  TFL_CHECK(!renderer.render(graph).ok() && !renderer.last_frame_stats().submitted);
  auto presentation = renderer::Presentation::create(device, {.extent = {0, 0}});
  TFL_CHECK(presentation.ok());
  TFL_CHECK(presentation.value().begin_frame().status().code ==
            base::StatusCode::out_of_date);
  TFL_CHECK(&renderer::RhiInterop::native(presentation.value().device()) == &gpu.device);
  TFL_CHECK(presentation.value().resize({16, 16}).ok());
  TFL_CHECK(presentation.value().begin_frame().ok());
  TFL_CHECK(!presentation.value().render(renderer, graph).ok());
  TFL_CHECK(presentation.value().begin_frame().ok());
  TFL_CHECK(presentation.value().cancel_frame().ok());
  renderer::FrameGraph windowGraph;
  TFL_CHECK(
      windowGraph.add_render_pass({.pass = {.colorAttachments = {{}}}, .useWindow = true})
          .ok());
  TFL_CHECK(presentation.value().begin_frame().ok());
  TFL_CHECK(presentation.value().render(renderer, windowGraph).ok());
  TFL_CHECK(renderer.last_frame_stats().submitted);
  TFL_CHECK(presentation.value().begin_frame().ok());
  TFL_CHECK(!presentation.value().render(renderer, renderer::FrameGraph{}).ok());
  TFL_CHECK(!renderer.last_frame_stats().submitted);
  auto owned = tests::make_null_context();
  auto adopted =
      renderer::RhiInterop::adopt(std::move(owned.instance), std::move(owned.device));
  TFL_CHECK(adopted.ok() && adopted.value().valid());
  renderer::PipelineCache programs(device);
  TFL_CHECK(!programs.register_program(1, {}).ok());
  {
    rhi::ShaderPackageDesc desc;
    desc.name = "Renderer consumer fixture";
    desc.sources = {
        {.path = "fixture",
         .language = rhi::ShaderSourceLanguage::backend_native,
         .sha256 = "df7e70e5021544f4834bbee64a9e3789febc4be81470df629cad6ddb03320a5c"}};
    desc.compilers = {{.name = "Null fixture", .version = "1"}};
    for (const auto stage : {rhi::ShaderStage::vertex, rhi::ShaderStage::fragment})
      desc.variants.push_back({
          .target = rhi::ShaderTarget::metal_source,
          .format = rhi::ShaderByteFormat::native_source,
          .kind = rhi::ShaderVariantKind::native_override,
          .stage = stage,
          .code = {std::byte{0x42}},
      });
    auto rawPackage = rhi::ShaderPackage::create(std::move(desc));
    TFL_CHECK(rawPackage.ok());
    auto package = renderer::ShaderPackage::load(rawPackage.value().bytes());
    TFL_CHECK(package.ok());
    TFL_CHECK(programs
                  .register_program(9,
                                    {
                                        .vertex = {&package.value()},
                                        .fragment = {&package.value()},
                                        .pipeline = {.layout = &layout.value()},
                                    })
                  .ok());
  }
  TFL_CHECK(programs.published(9));
  TFL_CHECK(!programs.register_program(9, {}).ok() && programs.published(9));
  TFL_CHECK(
      !renderer::SampledTexture::render_target(
           device, {.extent = {4, 4}, .format = static_cast<renderer::TextureFormat>(13)})
           .ok());
  TFL_CHECK(!renderer::Mesh{}.batch(1).ok());
}

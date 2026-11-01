#include "test_support.hpp"
#include "rhi_test_utils.hpp"

#include "truffle/renderer/frame_graph.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/renderer/transform_compute_pass.hpp"
#include "truffle/rhi/null_backend.hpp"

int main() {
    auto context = truffle::tests::make_null_context();

    auto shaderResult = context.device.create_shader({
        .stage = truffle::rhi::ShaderStage::compute,
        .code = {std::byte{0x42}},
    });
    TFL_CHECK(shaderResult.ok());
    auto shader = std::move(shaderResult).value();

    truffle::renderer::TransformComputePass pass(context.device, &shader);

    auto localBufferResult = context.device.create_buffer({
        .size = 4096,
        .usage = truffle::rhi::BufferUsage::storage,
    });
    auto parentBufferResult = context.device.create_buffer({
        .size = 4096,
        .usage = truffle::rhi::BufferUsage::storage,
    });
    auto outputBufferResult = context.device.create_buffer({
        .size = 4096,
        .usage = truffle::rhi::BufferUsage::storage,
    });
    TFL_CHECK(localBufferResult.ok());
    TFL_CHECK(parentBufferResult.ok());
    TFL_CHECK(outputBufferResult.ok());

    auto localBuffer = std::move(localBufferResult).value();
    auto parentBuffer = std::move(parentBufferResult).value();
    auto outputBuffer = std::move(outputBufferResult).value();

    truffle::renderer::TransformComputePassDesc computeDesc{
        .localTransformBuffer = &localBuffer,
        .parentIndexBuffer = &parentBuffer,
        .outTransformBuffer = &outputBuffer,
        .nodeCount = 64,
    };

    truffle::renderer::RenderBatch batch{};
    batch.vertexCount = 3;
    batch.instanceCount = 1;
    batch.kind = truffle::renderer::DrawKind::Direct;

    truffle::renderer::FrameGraph graph;
    const auto computeNode = graph.add_node(
        std::make_unique<truffle::renderer::ComputePassNode>(pass, computeDesc));
    const auto gbufferNode = graph.add_node(
        std::make_unique<truffle::renderer::RenderPassNode>(true, std::vector<truffle::renderer::RenderBatch>{batch}));
    const auto lightingNode = graph.add_node(
        std::make_unique<truffle::renderer::RenderPassNode>(true, std::vector<truffle::renderer::RenderBatch>{batch}));

    TFL_CHECK(graph.add_resource_usage(computeNode, {
                      .resourceId = 1,
                      .access = truffle::renderer::ResourceAccess::Write,
                  }).ok());
    TFL_CHECK(graph.add_resource_usage(gbufferNode, {
                      .resourceId = 1,
                      .access = truffle::renderer::ResourceAccess::Read,
                  }).ok());
    TFL_CHECK(graph.add_resource_usage(gbufferNode, {
                      .resourceId = 2,
                      .access = truffle::renderer::ResourceAccess::Write,
                  }).ok());
    TFL_CHECK(graph.add_resource_usage(lightingNode, {
                      .resourceId = 2,
                      .access = truffle::renderer::ResourceAccess::Read,
                  }).ok());

    truffle::renderer::Renderer renderer(context.device);
    TFL_CHECK(renderer.render(graph).ok());

    const auto& stats = renderer.last_frame_stats();
    TFL_CHECK(stats.computeNodesExecuted == 1);
    TFL_CHECK(stats.renderNodesExecuted == 2);
    TFL_CHECK(stats.renderBatchesExecuted == 2);
    TFL_CHECK(!stats.presented);

    TFL_CHECK(context.instance.stats().drawsRecorded == 2);
    TFL_CHECK(context.instance.stats().dispatchesRecorded == 1);
    TFL_CHECK(context.instance.stats().submissions == 1);

    return 0;
}

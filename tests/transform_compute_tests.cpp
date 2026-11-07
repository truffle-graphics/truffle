#include "truffle/renderer/specialist/transform_compute_pass.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/specialist/renderer.hpp"
#include "truffle/renderer/specialist/frame_graph.hpp"

using namespace truffle;

int main() {
    auto context = tests::make_null_context();

    auto shader = context.device.create_shader({
        .stage = rhi::ShaderStage::compute,
        .code = {std::byte{0x00}},
    }).value();

    renderer::specialist::TransformComputePass pass(context.device, &shader);

    auto localBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TFL_CHECK(localBufRes.ok());
    auto localBuf = std::move(localBufRes.value());

    auto parentBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TFL_CHECK(parentBufRes.ok());
    auto parentBuf = std::move(parentBufRes.value());

    auto outBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TFL_CHECK(outBufRes.ok());
    auto outBuf= std::move(outBufRes.value());

    renderer::specialist::TransformComputePassDesc desc{
        .localTransformBuffer = &localBuf,
        .parentIndexBuffer = &parentBuf,
        .outTransformBuffer = &outBuf,
        .nodeCount = 100,
    };

    // We no longer call pass.dispatch directly. We use FrameGraph.
    auto graph = [&]() {
        truffle::renderer::specialist::FrameGraph fg;
        fg.add_node(std::make_unique<truffle::renderer::specialist::ComputePassNode>(pass, desc));
        return fg;
    }();
    TFL_CHECK(truffle::renderer::specialist::Renderer{context.device}.render(graph).ok());

    // We no longer need cmd
    

    TFL_CHECK(context.instance.stats().dispatchesRecorded == 1);

    return 0;
}

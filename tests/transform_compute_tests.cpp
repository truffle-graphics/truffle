#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/render/frame_graph.hpp"

using namespace truffle;

int main() {
    auto context = tests::make_null_context();

    auto shader = context.device.create_shader({
        .stage = rhi::ShaderStage::compute,
        .code = {std::byte{0x00}},
    }).value();

    render::TransformComputePass pass(context.device, &shader);

    auto localBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(localBufRes.ok());
    auto localBuf = std::move(localBufRes.value());

    auto parentBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(parentBufRes.ok());
    auto parentBuf = std::move(parentBufRes.value());

    auto outBufRes = context.device.create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(outBufRes.ok());
    auto outBuf= std::move(outBufRes.value());

    render::TransformComputePassDesc desc{
        .localTransformBuffer = &localBuf,
        .parentIndexBuffer = &parentBuf,
        .outTransformBuffer = &outBuf,
        .nodeCount = 100,
    };

    // We no longer call pass.dispatch directly. We use FrameGraph.
    auto graph = [&]() {
        truffle::render::FrameGraph fg;
        fg.add_node(std::make_unique<truffle::render::ComputePassNode>(pass, desc));
        return fg;
    }();
    TRUFFLE_CHECK(truffle::render::Renderer{context.device}.render(graph).ok());

    // We no longer need cmd
    

    TRUFFLE_CHECK(context.instance.stats().dispatchesRecorded == 1);

    return 0;
}

#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "test_support.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/render/frame_graph.hpp"

using namespace truffle;

int main() {
    auto backend = rhi::create_null_backend();
    auto device = backend->create_device({}).value();

    auto shader = device->create_shader({
        .stage = rhi::ShaderStage::compute,
        .bytecode = {std::byte{0x00}},
    }).value();

    render::TransformComputePass pass(*device, shader.get());

        auto localBufRes = device->create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(localBufRes.ok());
    auto localBuf = std::move(localBufRes.value());

    auto parentBufRes = device->create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(parentBufRes.ok());
    auto parentBuf = std::move(parentBufRes.value());

    auto outBufRes = device->create_buffer({.size = 4096, .usage = rhi::BufferUsage::storage});
    TRUFFLE_CHECK(outBufRes.ok());
    auto outBuf= std::move(outBufRes.value());

    render::TransformComputePassDesc desc{
        .localTransformBuffer = localBuf.get(),
        .parentIndexBuffer = parentBuf.get(),
        .outTransformBuffer = outBuf.get(),
        .nodeCount = 100,
    };

    // We no longer call pass.dispatch directly. We use FrameGraph.
    auto graph = [&]() {
        truffle::render::FrameGraph fg;
        fg.add_node(std::make_unique<truffle::render::ComputePassNode>(pass, desc));
        return fg;
    }();
    TRUFFLE_CHECK(truffle::render::Renderer{*device}.render(graph).ok());

    // We no longer need cmd
    

    // Verify stats 1 dispatch was actually mocking as draw via device->stats()
    // NullBackend uses drawsRecorded for dispatch_compute too
    TRUFFLE_CHECK(backend->stats().drawsRecorded == 1);

    return 0;
}

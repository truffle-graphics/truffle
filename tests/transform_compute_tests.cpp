#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "test_support.hpp"

using namespace truffle;

int main() {
    auto backend = rhi::create_null_backend();
    auto device = backend->create_device({}).value();

    auto shader = device->create_shader({.bytecode={std::byte{0x00}}}).value();

    render::TransformComputePass pass(*device, shader.get());

    auto cmd = device->create_command_buffer();
    TRUFFLE_CHECK(cmd->begin().ok());

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

    auto s = pass.dispatch(*cmd, desc);
    TRUFFLE_CHECK(s.ok());
    TRUFFLE_CHECK(cmd->end().ok());

    // Verify stats 1 dispatch was actually mocking as draw via device->stats()
    // NullBackend uses drawsRecorded for dispatch_compute too
    TRUFFLE_CHECK(backend->stats().drawsRecorded == 1);

    return 0;
}

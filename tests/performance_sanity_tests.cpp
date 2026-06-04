#include "test_support.hpp"

#include "truffle/rhi/null_backend.hpp"

#include <chrono>

int main() {
    auto backend = truffle::rhi::create_null_backend();
    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    auto ringResult = device->create_upload_ring(3, 64 * 1024);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    const auto start = std::chrono::steady_clock::now();

    for (std::uint32_t i = 0; i < 50000; ++i) {
        const auto allocation = ring->allocate(256, 16);
        TRUFFLE_CHECK(allocation.valid());
        ring->advance();
    }

    for (std::uint32_t i = 0; i < 10000; ++i) {
        auto cmd = device->create_command_buffer();
        TRUFFLE_CHECK(cmd->begin().ok());
        TRUFFLE_CHECK(cmd->end().ok());
        TRUFFLE_CHECK(device->queue(truffle::rhi::QueueKind::graphics).submit(*cmd).ok());
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Performance sanity gate: this should remain well below threshold on CI hardware.
    TRUFFLE_CHECK(elapsedMs < 8000);

    return 0;
}

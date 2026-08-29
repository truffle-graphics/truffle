#include "test_support.hpp"
#include "rhi_test_utils.hpp"

#include "truffle/rhi/null_backend.hpp"

#include <array>
#include <chrono>

int main() {
    auto context = truffle::tests::make_null_context();

    auto ringResult = context.device.create_upload_ring(3, 64 * 1024);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    const auto start = std::chrono::steady_clock::now();

    for (std::uint32_t i = 0; i < 50000; ++i) {
        const auto allocation = ring.allocate(256, 16);
        TRUFFLE_CHECK(allocation.valid());
        TRUFFLE_CHECK(ring.advance().ok());
    }

    auto queueResult = context.device.queue(truffle::rhi::QueueKind::graphics);
    TRUFFLE_CHECK(queueResult.ok());
    auto queue = std::move(queueResult).value();
    auto poolResult =
        context.device.create_command_pool(truffle::rhi::QueueKind::graphics);
    TRUFFLE_CHECK(poolResult.ok());
    auto pool = std::move(poolResult).value();
    for (std::uint32_t i = 0; i < 10000; ++i) {
        auto listResult = pool.allocate();
        TRUFFLE_CHECK(listResult.ok());
        auto list = std::move(listResult).value();
        TRUFFLE_CHECK(list.begin().ok());
        TRUFFLE_CHECK(list.end().ok());
        std::array<truffle::rhi::CommandList*, 1> lists{&list};
        TRUFFLE_CHECK(queue.submit(lists).ok());
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Performance sanity gate: this should remain well below threshold on CI hardware.
    TRUFFLE_CHECK(elapsedMs < 8000);

    return 0;
}

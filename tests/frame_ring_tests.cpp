#include "test_support.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <cstring>

int main() {
    auto backend = truffle::rhi::create_null_backend();
    auto deviceResult = backend->create_device({});
    TRUFFLE_CHECK(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    // --- create_upload_ring rejects bad arguments ---
    TRUFFLE_CHECK(!device->create_upload_ring(0, 1024).ok());
    TRUFFLE_CHECK(!device->create_upload_ring(2, 0).ok());

    // --- Valid ring creation ---
    constexpr std::uint32_t kFrames    = 2;
    constexpr std::size_t   kCapacity  = 64 * 1024; // 64 KiB per frame
    auto ringResult = device->create_upload_ring(kFrames, kCapacity);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    TRUFFLE_CHECK(ring->frames_in_flight() == kFrames);
    TRUFFLE_CHECK(ring->capacity_per_frame() == kCapacity);

    // --- Allocate some memory and write to it ---
    constexpr std::size_t kAllocSize = 256;
    auto alloc = ring->allocate(kAllocSize);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.buffer != nullptr);
    TRUFFLE_CHECK(alloc.mappedPtr != nullptr);
    TRUFFLE_CHECK(alloc.size == kAllocSize);
    TRUFFLE_CHECK(!ring->allocate(1, 0).valid());
    TRUFFLE_CHECK(!ring->allocate(1, 3).valid());

    // Write into mapped memory (must not crash).
    std::memset(alloc.mappedPtr, 0xAB, kAllocSize);

    // --- Second allocation in same frame ---
    auto alloc2 = ring->allocate(kAllocSize, 64);
    TRUFFLE_CHECK(alloc2.valid());
    TRUFFLE_CHECK(alloc2.offset > alloc.offset);
    TRUFFLE_CHECK((alloc2.offset % 64) == 0);

    // --- Exhausting a frame returns invalid allocation ---
    auto bigAlloc = ring->allocate(kCapacity); // too large for what remains
    TRUFFLE_CHECK(!bigAlloc.valid());

    // --- Advance to next frame and allocate again ---
    ring->advance();
    auto alloc3 = ring->allocate(kAllocSize);
    TRUFFLE_CHECK(alloc3.valid());
    // offset in new frame starts at frame boundary
    TRUFFLE_CHECK(alloc3.offset >= kCapacity);

    // --- Advance wraps back to frame 0 ---
    ring->advance();
    auto alloc4 = ring->allocate(kAllocSize);
    TRUFFLE_CHECK(alloc4.valid());
    TRUFFLE_CHECK(alloc4.offset < kCapacity);

    return 0;
}

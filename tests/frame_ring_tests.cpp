#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <cstring>

int main() {
    auto context = truffle::tests::make_null_context();

    // --- create_upload_ring rejects bad arguments ---
    TFL_CHECK(!context.device.create_upload_ring(0, 1024).ok());
    TFL_CHECK(!context.device.create_upload_ring(2, 0).ok());

    // --- Valid ring creation ---
    constexpr std::uint32_t kFrames    = 2;
    constexpr std::size_t   kCapacity  = 64 * 1024; // 64 KiB per frame
    auto ringResult = context.device.create_upload_ring(kFrames, kCapacity);
    TFL_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    // --- Allocate some memory and write to it ---
    constexpr std::size_t kAllocSize = 256;
    auto alloc = ring.allocate(kAllocSize);
    TFL_CHECK(alloc.valid());
    TFL_CHECK(alloc.buffer != nullptr);
    TFL_CHECK(alloc.mapped != nullptr);
    TFL_CHECK(alloc.size == kAllocSize);
    TFL_CHECK(!ring.allocate(1, 0).valid());
    TFL_CHECK(!ring.allocate(1, 3).valid());

    // Write into mapped memory (must not crash).
    std::memset(alloc.mapped, 0xAB, kAllocSize);

    // --- Second allocation in same frame ---
    auto alloc2 = ring.allocate(kAllocSize, 64);
    TFL_CHECK(alloc2.valid());
    TFL_CHECK(alloc2.offset > alloc.offset);
    TFL_CHECK((alloc2.offset % 64) == 0);

    // --- Exhausting a frame returns invalid allocation ---
    auto bigAlloc = ring.allocate(kCapacity); // too large for what remains
    TFL_CHECK(!bigAlloc.valid());

    // --- Advance to next frame and allocate again ---
    TFL_CHECK(ring.advance().ok());
    auto alloc3 = ring.allocate(kAllocSize);
    TFL_CHECK(alloc3.valid());
    TFL_CHECK(alloc3.offset == 0);

    // --- Advance wraps back to frame 0 ---
    TFL_CHECK(ring.advance().ok());
    auto alloc4 = ring.allocate(kAllocSize);
    TFL_CHECK(alloc4.valid());
    TFL_CHECK(alloc4.offset < kCapacity);

    return 0;
}

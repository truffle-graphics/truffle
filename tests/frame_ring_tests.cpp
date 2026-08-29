#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <cstring>

int main() {
    auto context = truffle::tests::make_null_context();

    // --- create_upload_ring rejects bad arguments ---
    TRUFFLE_CHECK(!context.device.create_upload_ring(0, 1024).ok());
    TRUFFLE_CHECK(!context.device.create_upload_ring(2, 0).ok());

    // --- Valid ring creation ---
    constexpr std::uint32_t kFrames    = 2;
    constexpr std::size_t   kCapacity  = 64 * 1024; // 64 KiB per frame
    auto ringResult = context.device.create_upload_ring(kFrames, kCapacity);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    // --- Allocate some memory and write to it ---
    constexpr std::size_t kAllocSize = 256;
    auto alloc = ring.allocate(kAllocSize);
    TRUFFLE_CHECK(alloc.valid());
    TRUFFLE_CHECK(alloc.buffer != nullptr);
    TRUFFLE_CHECK(alloc.mapped != nullptr);
    TRUFFLE_CHECK(alloc.size == kAllocSize);
    TRUFFLE_CHECK(!ring.allocate(1, 0).valid());
    TRUFFLE_CHECK(!ring.allocate(1, 3).valid());

    // Write into mapped memory (must not crash).
    std::memset(alloc.mapped, 0xAB, kAllocSize);

    // --- Second allocation in same frame ---
    auto alloc2 = ring.allocate(kAllocSize, 64);
    TRUFFLE_CHECK(alloc2.valid());
    TRUFFLE_CHECK(alloc2.offset > alloc.offset);
    TRUFFLE_CHECK((alloc2.offset % 64) == 0);

    // --- Exhausting a frame returns invalid allocation ---
    auto bigAlloc = ring.allocate(kCapacity); // too large for what remains
    TRUFFLE_CHECK(!bigAlloc.valid());

    // --- Advance to next frame and allocate again ---
    TRUFFLE_CHECK(ring.advance().ok());
    auto alloc3 = ring.allocate(kAllocSize);
    TRUFFLE_CHECK(alloc3.valid());
    TRUFFLE_CHECK(alloc3.offset == 0);

    // --- Advance wraps back to frame 0 ---
    TRUFFLE_CHECK(ring.advance().ok());
    auto alloc4 = ring.allocate(kAllocSize);
    TRUFFLE_CHECK(alloc4.valid());
    TRUFFLE_CHECK(alloc4.offset < kCapacity);

    return 0;
}

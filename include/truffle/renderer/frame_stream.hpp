#pragma once

#include "truffle/renderer/render_batch.hpp"
#include "truffle/renderer/renderer.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace truffle::renderer {

// Frames-in-flight pacing over one monotonically signaled fence. Typical loop:
//   slot = pacer.begin_frame(); stream.begin_frame(slot); ...write/record...;
//   renderer.render(graph, swapchain, pacer.signal());
//   pacer.end_frame(renderer.last_frame_stats().submitted);
// Single-thread-owned; the device outlives it.
class FramePacer {
public:
    FramePacer() = default;
    FramePacer(FramePacer&&) noexcept = default;
    FramePacer& operator=(FramePacer&&) noexcept = default;
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    [[nodiscard]] static base::Result<FramePacer> create(rhi::Device& device,
                                                         std::uint32_t framesInFlight = 2);
    // Waits until the GPU finished the last submitted frame that used the next
    // slot, then returns that slot. CPU data owned by the slot may be rewritten.
    [[nodiscard]] base::Result<std::uint32_t> begin_frame(
        std::chrono::nanoseconds timeout = std::chrono::seconds(10));
    // Completion signal for the frame opened by begin_frame().
    [[nodiscard]] RenderSignal signal() noexcept;
    // Pass RendererFrameStats::submitted. Unsubmitted frames are never waited on.
    void end_frame(bool submitted) noexcept;
    // Waits for every submitted frame (for example before destroying resources).
    [[nodiscard]] base::Status wait_idle(
        std::chrono::nanoseconds timeout = std::chrono::seconds(10));
    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept {
        return static_cast<std::uint32_t>(slotValues_.size());
    }

private:
    rhi::Fence fence_;
    std::vector<std::uint64_t> slotValues_; // last submitted value per slot, 0 if none
    std::uint64_t nextValue_ = 0;
    std::uint64_t lastSubmitted_ = 0;
    std::uint32_t slot_ = 0;
    bool open_ = false;
};

struct StreamBufferDesc {
    rhi::BufferUsage usage = rhi::BufferUsage::vertex | rhi::BufferUsage::index |
                             rhi::BufferUsage::uniform;
    std::size_t initialBytes = 64 * 1024;
    std::uint32_t framesInFlight = 2;
};

// Per-frame CPU-written bytes (geometry, uniforms) rebuilt every frame. Each
// slot owns upload-domain buffers; writes never touch another slot's memory.
// Views stay valid until the same slot begins again, including after growth.
// Bytes are copied unchanged; their meaning is an application-shader contract.
class StreamBuffer {
public:
    StreamBuffer() = default;
    StreamBuffer(StreamBuffer&&) noexcept = default;
    StreamBuffer& operator=(StreamBuffer&&) noexcept = default;
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    [[nodiscard]] static base::Result<StreamBuffer> create(rhi::Device& device,
                                                           const StreamBufferDesc& desc = {});
    // `slot` comes from FramePacer::begin_frame(), whose wait makes reuse safe.
    [[nodiscard]] base::Status begin_frame(std::uint32_t slot);
    // Copies `bytes` at a power-of-two alignment (at least 4), growing the slot
    // when needed. Empty writes are rejected.
    [[nodiscard]] base::Result<BufferView> write(std::span<const std::byte> bytes,
                                                 std::size_t alignment = 16);

private:
    struct Slot {
        std::vector<std::unique_ptr<rhi::Buffer>> buffers; // back() is current
        std::size_t offset = 0;
    };
    rhi::Device* device_ = nullptr;
    rhi::BufferUsage usage_ = rhi::BufferUsage::none;
    std::vector<Slot> slots_;
    std::uint32_t slot_ = 0;
    bool open_ = false;
};

} // namespace truffle::renderer

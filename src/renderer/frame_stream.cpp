#include "truffle/renderer/frame_stream.hpp"

#include <algorithm>

namespace truffle::renderer {
namespace {

base::Status invalid(base::StatusCode code, const char* message) {
    return base::Status::failure(code, message);
}

} // namespace

base::Result<FramePacer> FramePacer::create(rhi::Device& device,
                                            std::uint32_t framesInFlight) {
    if (framesInFlight == 0) {
        return invalid(base::StatusCode::invalid_argument,
                       "FramePacer: at least one frame in flight required");
    }
    auto fence = device.create_fence({.debugName = "Renderer frame pacer"});
    if (!fence.ok()) {
        return fence.status();
    }
    FramePacer pacer;
    pacer.fence_ = std::move(fence).value();
    pacer.slotValues_.assign(framesInFlight, 0);
    return pacer;
}

base::Result<std::uint32_t> FramePacer::begin_frame(std::chrono::nanoseconds timeout) {
    if (!fence_.valid() || open_) {
        return invalid(base::StatusCode::invalid_state,
                       "FramePacer: uncreated, or previous frame not ended");
    }
    const auto slot = static_cast<std::uint32_t>(nextValue_ % slotValues_.size());
    if (slotValues_[slot] != 0) {
        if (auto status = fence_.wait(slotValues_[slot], timeout); !status.ok()) {
            return status;
        }
    }
    ++nextValue_;
    slot_ = slot;
    open_ = true;
    return slot;
}

RenderSignal FramePacer::signal() noexcept {
    return open_ ? RenderSignal{&fence_, nextValue_} : RenderSignal{};
}

void FramePacer::end_frame(bool submitted) noexcept {
    if (!open_) {
        return;
    }
    if (submitted) {
        slotValues_[slot_] = nextValue_;
        lastSubmitted_ = nextValue_;
    }
    open_ = false;
}

base::Status FramePacer::wait_idle(std::chrono::nanoseconds timeout) {
    if (lastSubmitted_ == 0) {
        return base::Status::success();
    }
    return fence_.wait(lastSubmitted_, timeout);
}

base::Result<StreamBuffer> StreamBuffer::create(rhi::Device& device,
                                                const StreamBufferDesc& desc) {
    if (desc.framesInFlight == 0 || desc.initialBytes == 0 ||
        desc.usage == rhi::BufferUsage::none) {
        return invalid(base::StatusCode::invalid_argument,
                       "StreamBuffer: frames, initial size and usage must be nonzero");
    }
    StreamBuffer stream;
    stream.device_ = &device;
    stream.usage_ = desc.usage;
    stream.slots_.resize(desc.framesInFlight);
    for (auto& slot : stream.slots_) {
        auto buffer = device.create_buffer({
            .size = desc.initialBytes, .usage = desc.usage,
            .memory = rhi::MemoryDomain::upload, .debugName = "Renderer stream",
        });
        if (!buffer.ok()) {
            return buffer.status();
        }
        slot.buffers.push_back(std::make_unique<rhi::Buffer>(std::move(buffer).value()));
    }
    return stream;
}

base::Status StreamBuffer::begin_frame(std::uint32_t slot) {
    if (slot >= slots_.size()) {
        return invalid(base::StatusCode::invalid_argument,
                       "StreamBuffer: slot outside frames in flight");
    }
    auto& current = slots_[slot];
    // The pacer waited for this slot's previous frame; keep only the largest buffer.
    current.buffers.erase(current.buffers.begin(), current.buffers.end() - 1);
    current.offset = 0;
    slot_ = slot;
    open_ = true;
    return base::Status::success();
}

base::Result<BufferView> StreamBuffer::write(std::span<const std::byte> bytes,
                                             std::size_t alignment) {
    if (!open_ || bytes.empty() || alignment < 4 || (alignment & (alignment - 1)) != 0) {
        return invalid(base::StatusCode::invalid_argument,
                       "StreamBuffer: begin a frame and write nonempty, power-of-two aligned bytes");
    }
    auto& slot = slots_[slot_];
    auto* buffer = slot.buffers.back().get();
    const auto capacity = buffer->desc().size;
    auto offset = (slot.offset + alignment - 1) & ~(alignment - 1);
    if (offset > capacity || bytes.size() > capacity - offset) {
        auto grown = device_->create_buffer({
            .size = std::max(capacity * 2, bytes.size()), .usage = usage_,
            .memory = rhi::MemoryDomain::upload, .debugName = "Renderer stream",
        });
        if (!grown.ok()) {
            return grown.status();
        }
        slot.buffers.push_back(std::make_unique<rhi::Buffer>(std::move(grown).value()));
        buffer = slot.buffers.back().get();
        offset = 0;
    }
    if (auto status = buffer->write(offset, bytes); !status.ok()) {
        return status;
    }
    slot.offset = offset + bytes.size();
    return BufferView{buffer, offset, bytes.size()};
}

} // namespace truffle::renderer

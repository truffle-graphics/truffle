#pragma once

#include "truffle/renderer/renderer.hpp"

#include <chrono>

namespace truffle::renderer {

class FramePacer {
public:
  FramePacer() = default;
  FramePacer(FramePacer &&) noexcept = default;
  FramePacer &operator=(FramePacer &&) noexcept = default;
  FramePacer(const FramePacer &) = delete;
  FramePacer &operator=(const FramePacer &) = delete;
  [[nodiscard]] static base::Result<FramePacer> create(Device &,
                                                       std::uint32_t frames = 2);
  [[nodiscard]] base::Result<std::uint32_t>
  begin_frame(std::chrono::nanoseconds timeout = std::chrono::seconds(10));
  [[nodiscard]] RenderSignal signal();
  void end_frame(bool submitted) noexcept;
  [[nodiscard]] base::Status
  wait_idle(std::chrono::nanoseconds timeout = std::chrono::seconds(10));

private:
  struct State;
  std::shared_ptr<State> state_;
};
struct StreamBufferDesc {
  BufferUsage usage = BufferUsage::vertex | BufferUsage::index | BufferUsage::uniform;
  std::size_t initialBytes = 64 * 1024;
  std::uint32_t framesInFlight = 2;
};
class StreamBuffer {
public:
  StreamBuffer() = default;
  StreamBuffer(StreamBuffer &&) noexcept = default;
  StreamBuffer &operator=(StreamBuffer &&) noexcept = default;
  StreamBuffer(const StreamBuffer &) = delete;
  StreamBuffer &operator=(const StreamBuffer &) = delete;
  [[nodiscard]] static base::Result<StreamBuffer> create(Device &,
                                                         const StreamBufferDesc & = {});
  [[nodiscard]] base::Status begin_frame(std::uint32_t slot);
  [[nodiscard]] base::Result<BufferSlice> write(std::span<const std::byte>,
                                                std::size_t alignment = 16);

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace truffle::renderer

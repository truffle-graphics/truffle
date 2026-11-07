#pragma once

#include "truffle/renderer/frame_stream.hpp"

#include <chrono>
#include <memory>
#include <optional>

namespace truffle::renderer {

struct PresentationDesc {
  rhi::NativeSurface native;
  rhi::Extent2D extent{1, 1}; // physical pixels; either zero dimension suspends
  rhi::TextureFormat format = rhi::TextureFormat::bgra8_unorm;
  rhi::PresentMode presentMode = rhi::PresentMode::fifo;
  std::uint32_t imageCount = 2;
  std::uint32_t framesInFlight = 2;
};

struct PresentationFrame {
  std::uint32_t slot = 0;
  rhi::Extent2D extent;
};

// Graphics-side presentation only. The host owns its native handle, event loop
// and input. One host thread owns this object. An explicit device is borrowed
// and must remain alive and unmoved. Owned devices have a stable address even
// when Presentation moves.
class Presentation {
public:
  Presentation();
  ~Presentation();
  Presentation(Presentation &&) noexcept;
  Presentation &operator=(Presentation &&) noexcept;
  Presentation(const Presentation &) = delete;
  Presentation &operator=(const Presentation &) = delete;

  [[nodiscard]] static base::Result<Presentation> create(rhi::Device &device,
                                                         const PresentationDesc &desc);
  // Owns the instance and selects a presentation-capable graphics device.
  [[nodiscard]] static base::Result<Presentation> create(rhi::Instance instance,
                                                         const PresentationDesc &desc,
                                                         rhi::DeviceDesc deviceDesc = {});

  [[nodiscard]] bool valid() const noexcept;
  // Accessors require valid(). Device/cache/resources outlive their users.
  [[nodiscard]] rhi::Device &device() const noexcept;
  [[nodiscard]] rhi::Swapchain *swapchain() const noexcept;
  [[nodiscard]] const PresentationDesc &desc() const noexcept;
  [[nodiscard]] const RendererFrameStats &last_frame_stats() const noexcept;

  // Report size before beginning a frame. Zero size performs no acquisition,
  // allocation or slot reuse; begin_frame then reports out_of_date.
  [[nodiscard]] base::Status resize(rhi::Extent2D extent);
  // Retire graphics objects before the host destroys the old native handle.
  // A lost device instead requires a new Presentation (and new GPU resources).
  [[nodiscard]] base::Status
  replace_surface(rhi::NativeSurface native, rhi::Extent2D extent,
                  std::chrono::nanoseconds timeout = std::chrono::seconds(10));
  [[nodiscard]] base::Result<PresentationFrame>
  begin_frame(std::chrono::nanoseconds timeout = std::chrono::seconds(10));
  // Executes the caller's complete graph on the same device and closes the
  // frame even on failure. At least one node must use the swapchain. Recovery
  // is deferred to the next begin_frame, never replaying application work.
  [[nodiscard]] base::Status render(Renderer &renderer, const FrameGraph &graph);
  [[nodiscard]] base::Status cancel_frame();
  // Call before destroying borrowed host handles, caches or GPU resources.
  [[nodiscard]] base::Status
  wait_idle(std::chrono::nanoseconds timeout = std::chrono::seconds(10));

private:
  struct State;
  std::unique_ptr<State> state_;
  [[nodiscard]] base::Status initialize();
  [[nodiscard]] base::Status prepare_surface(std::chrono::nanoseconds timeout);
};

struct NativePresentationOptions {
  // Automatic: Metal for CAMetalLayer (Vulkan if Metal is not built), Vulkan
  // for Win32/XCB. Other native kinds and backends are explicitly unsupported.
  std::optional<rhi::BackendKind> backend;
  rhi::InstanceDesc instance;
  rhi::DeviceDesc device;
};

// Optional native factory: link Truffle::RendererPresentation. Core Renderer
// neither defines this function nor links backend implementations. No backend
// header or platform graphics type is needed by the caller.
[[nodiscard]] base::Result<Presentation>
create_native_presentation(const PresentationDesc &desc,
                           const NativePresentationOptions &options = {});

} // namespace truffle::renderer

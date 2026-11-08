#pragma once

#include "truffle/renderer/frame_stream.hpp"

namespace truffle::renderer {

struct PresentationDesc {
  WindowHandle native;
  PixelSize extent{1, 1};
  TextureFormat format = TextureFormat::bgra8_unorm;
  PresentMode presentMode = PresentMode::fifo;
  std::uint32_t imageCount = 2, framesInFlight = 2;
};
struct PresentationFrame {
  std::uint32_t slot = 0;
  PixelSize extent;
};
struct NativePresentationOptions {
  Backend backend = Backend::automatic;
  bool enableValidation = true;
};
class Presentation {
public:
  Presentation();
  ~Presentation();
  Presentation(Presentation &&) noexcept;
  Presentation &operator=(Presentation &&) noexcept;
  Presentation(const Presentation &) = delete;
  Presentation &operator=(const Presentation &) = delete;
  [[nodiscard]] static base::Result<Presentation> create(Device &,
                                                         const PresentationDesc &);
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Device &device() noexcept;
  [[nodiscard]] const PresentationDesc &desc() const noexcept;
  [[nodiscard]] RendererFrameStats last_frame_stats() const noexcept;
  [[nodiscard]] base::Status resize(PixelSize);
  [[nodiscard]] base::Status
  replace_surface(WindowHandle, PixelSize,
                  std::chrono::nanoseconds timeout = std::chrono::seconds(10));
  [[nodiscard]] base::Result<PresentationFrame>
  begin_frame(std::chrono::nanoseconds timeout = std::chrono::seconds(10));
  [[nodiscard]] base::Status render(Renderer &, const FrameGraph &);
  [[nodiscard]] base::Status cancel_frame();
  [[nodiscard]] base::Status
  wait_idle(std::chrono::nanoseconds timeout = std::chrono::seconds(10));

private:
  struct State;
  std::unique_ptr<State> state_;
  friend struct RhiInterop;
};
// Optional native factory; borrowed Device creation above is in core Renderer.
[[nodiscard]] base::Result<Presentation>
create_native_presentation(const PresentationDesc &,
                           const NativePresentationOptions & = {});

} // namespace truffle::renderer

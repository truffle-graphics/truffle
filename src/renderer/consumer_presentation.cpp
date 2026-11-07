#include "consumer_convert.hpp"

#include <cassert>

namespace truffle::renderer {

struct Presentation::State {
  Device device;
  specialist::Presentation raw;
  PresentationDesc desc;
};
Presentation::Presentation() = default;
Presentation::~Presentation() = default;
Presentation::Presentation(Presentation &&) noexcept = default;
Presentation &Presentation::operator=(Presentation &&) noexcept = default;
base::Result<Presentation> Presentation::create(Device &device,
                                                const PresentationDesc &desc) {
  if (!device.valid())
    return detail::invalid("Presentation requires a valid device");
  if (!detail::valid_format(desc.format))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer presentation format");
  auto raw = specialist::Presentation::create(
      RhiInterop::native(device),
      {
          .native = desc.native,
          .extent = detail::extent(desc.extent),
          .format = detail::enum_value<rhi::TextureFormat>(desc.format),
          .presentMode = detail::enum_value<rhi::PresentMode>(desc.presentMode),
          .imageCount = desc.imageCount,
          .framesInFlight = desc.framesInFlight,
      });
  if (!raw.ok())
    return raw.status();
  Presentation result;
  result.state_ = std::make_unique<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  result.state_->desc = desc;
  return result;
}
bool Presentation::valid() const noexcept { return state_ && state_->raw.valid(); }
Device &Presentation::device() noexcept {
  assert(valid());
  return state_->device;
}
const PresentationDesc &Presentation::desc() const noexcept {
  assert(valid());
  return state_->desc;
}
specialist::Presentation &RhiInterop::native(Presentation &value) {
  assert(value.valid());
  return value.state_->raw;
}
RendererFrameStats Presentation::last_frame_stats() const noexcept {
  return valid() ? detail::stats(state_->raw.last_frame_stats()) : RendererFrameStats{};
}
base::Status Presentation::resize(PixelSize extent) {
  if (!valid())
    return detail::invalid("Presentation is not created");
  auto status = state_->raw.resize(detail::extent(extent));
  if (status.ok())
    state_->desc.extent = extent;
  return status;
}
base::Status Presentation::replace_surface(WindowHandle window, PixelSize extent,
                                           std::chrono::nanoseconds timeout) {
  if (!valid())
    return detail::invalid("Presentation is not created");
  auto status = state_->raw.replace_surface(window, detail::extent(extent), timeout);
  if (status.ok()) {
    state_->desc.native = window;
    state_->desc.extent = extent;
  }
  return status;
}
base::Result<PresentationFrame>
Presentation::begin_frame(std::chrono::nanoseconds timeout) {
  if (!valid())
    return detail::invalid("Presentation is not created");
  auto raw = state_->raw.begin_frame(timeout);
  if (!raw.ok())
    return raw.status();
  return PresentationFrame{raw.value().slot, detail::pixels(raw.value().extent)};
}
base::Status Presentation::render(Renderer &renderer, const FrameGraph &graph) {
  if (!valid())
    return detail::invalid("Presentation is not created");
  renderer.set_frame_stats({});
  if (!renderer.valid()) {
    auto cancelled = cancel_frame();
    return cancelled.ok() ? detail::invalid("Renderer is not initialized") : cancelled;
  }
  if (auto order = graph.resolve_execution_order(); !order.ok()) {
    auto cancelled = cancel_frame();
    return cancelled.ok() ? order.status() : cancelled;
  }
  auto status =
      state_->raw.render(RhiInterop::native(renderer), RhiInterop::native(graph));
  renderer.set_frame_stats(last_frame_stats());
  return status;
}
base::Status Presentation::cancel_frame() {
  return valid() ? state_->raw.cancel_frame()
                 : detail::invalid("Presentation is not created");
}
base::Status Presentation::wait_idle(std::chrono::nanoseconds timeout) {
  return valid() ? state_->raw.wait_idle(timeout)
                 : detail::invalid("Presentation is not created");
}

} // namespace truffle::renderer

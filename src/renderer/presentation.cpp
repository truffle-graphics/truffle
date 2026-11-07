#include "truffle/renderer/presentation.hpp"

#include "truffle/rhi/validation.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace truffle::renderer {
namespace {

base::Status invalid(base::StatusCode code, const char *message) {
  return base::Status::failure(code, message);
}

base::Status validate(const PresentationDesc &desc) {
  if (!rhi::validation::native_surface_handles_valid(desc.native) ||
      desc.imageCount == 0 || desc.framesInFlight == 0) {
    return invalid(
        base::StatusCode::invalid_argument,
        "Presentation: valid native handles and nonzero image/frame counts required");
  }
  return base::Status::success();
}

bool suspended(rhi::Extent2D extent) { return extent.width == 0 || extent.height == 0; }

} // namespace

struct Presentation::State {
  rhi::Instance instance;
  rhi::Device ownedDevice;
  rhi::Device *device = nullptr;
  rhi::Surface surface;
  rhi::Swapchain swapchain;
  FramePacer pacer;
  PresentationDesc desc;
  RendererFrameStats stats;
  base::Status surfaceStatus;
  bool rebuild = true;
  bool open = false;
};

Presentation::Presentation() = default;
Presentation::~Presentation() = default;
Presentation::Presentation(Presentation &&) noexcept = default;
Presentation &Presentation::operator=(Presentation &&) noexcept = default;

base::Result<Presentation> Presentation::create(rhi::Device &device,
                                                const PresentationDesc &desc) {
  if (auto status = validate(desc); !status.ok()) {
    return status;
  }
  Presentation presentation;
  presentation.state_ = std::make_unique<State>();
  presentation.state_->device = &device;
  presentation.state_->desc = desc;
  if (auto status = presentation.initialize(); !status.ok()) {
    return status;
  }
  return presentation;
}

base::Result<Presentation> Presentation::create(rhi::Instance instance,
                                                const PresentationDesc &desc,
                                                rhi::DeviceDesc deviceDesc) {
  if (auto status = validate(desc); !status.ok()) {
    return status;
  }
  if (!instance.valid()) {
    return invalid(base::StatusCode::invalid_argument,
                   "Presentation: a valid instance is required");
  }
  if (std::find(deviceDesc.requiredFeatures.begin(), deviceDesc.requiredFeatures.end(),
                rhi::Feature::presentation) == deviceDesc.requiredFeatures.end()) {
    deviceDesc.requiredFeatures.push_back(rhi::Feature::presentation);
  }
  auto failure = invalid(base::StatusCode::unavailable,
                         "Presentation: no presentation-capable graphics adapter");
  for (std::size_t index = 0; index < instance.adapter_count(); ++index) {
    auto adapter = instance.adapter(index);
    if (!adapter.ok()) {
      return adapter.status();
    }
    const auto &info = adapter.value().info();
    if (!info.presentation ||
        !rhi::validation::supports_queue(info, rhi::QueueKind::graphics)) {
      continue;
    }
    auto device = adapter.value().request_device(deviceDesc);
    if (!device.ok()) {
      failure = device.status();
    } else {
      Presentation presentation;
      presentation.state_ = std::make_unique<State>();
      auto &state = *presentation.state_;
      state.ownedDevice = std::move(device).value();
      state.device = &state.ownedDevice;
      state.desc = desc;
      if (auto status = presentation.initialize(); status.ok()) {
        state.instance = std::move(instance);
        return presentation;
      } else {
        failure = std::move(status);
      }
    }
    if (failure.code != base::StatusCode::unsupported &&
        failure.code != base::StatusCode::unavailable) {
      return failure;
    }
  }
  return failure;
}

base::Status Presentation::initialize() {
  if (!state_->device->valid()) {
    return invalid(base::StatusCode::invalid_argument,
                   "Presentation: a valid device is required");
  }
  if (state_->device->lost()) {
    return invalid(base::StatusCode::device_lost, "Presentation: device is lost");
  }
  const auto &info = state_->device->adapter_info();
  if (!info.presentation ||
      !rhi::validation::supports_queue(info, rhi::QueueKind::graphics)) {
    return invalid(base::StatusCode::unsupported,
                   "Presentation: device needs presentation and a graphics queue");
  }
  auto pacer = FramePacer::create(*state_->device, state_->desc.framesInFlight);
  if (!pacer.ok()) {
    return pacer.status();
  }
  state_->pacer = std::move(pacer).value();
  return suspended(state_->desc.extent) ? base::Status::success()
                                        : prepare_surface(std::chrono::seconds(10));
}

bool Presentation::valid() const noexcept {
  return state_ != nullptr && state_->device->valid();
}

rhi::Device &Presentation::device() const noexcept {
  assert(valid());
  return *state_->device;
}

rhi::Swapchain *Presentation::swapchain() const noexcept {
  assert(valid());
  return state_->swapchain.valid() ? &state_->swapchain : nullptr;
}

const PresentationDesc &Presentation::desc() const noexcept {
  assert(valid());
  return state_->desc;
}

const RendererFrameStats &Presentation::last_frame_stats() const noexcept {
  assert(valid());
  return state_->stats;
}

base::Status Presentation::resize(rhi::Extent2D extent) {
  if (!valid() || state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Presentation: resize requires a valid object with no open frame");
  }
  if (state_->device->lost()) {
    return invalid(base::StatusCode::device_lost, "Presentation: device is lost");
  }
  if (state_->desc.extent != extent) {
    state_->desc.extent = extent;
    state_->rebuild = true;
  }
  return base::Status::success();
}

base::Status Presentation::replace_surface(rhi::NativeSurface native,
                                           rhi::Extent2D extent,
                                           std::chrono::nanoseconds timeout) {
  if (!rhi::validation::native_surface_handles_valid(native)) {
    return invalid(base::StatusCode::invalid_argument,
                   "Presentation: replacement native handles are invalid");
  }
  if (auto status = wait_idle(timeout); !status.ok()) {
    return status;
  }
  state_->swapchain = {};
  state_->surface = {};
  state_->desc.native = native;
  state_->desc.extent = extent;
  state_->surfaceStatus = {};
  state_->rebuild = true;
  return base::Status::success();
}

base::Status Presentation::prepare_surface(std::chrono::nanoseconds timeout) {
  if (!state_->surfaceStatus.ok()) {
    return state_->surfaceStatus;
  }
  if (!state_->rebuild) {
    return base::Status::success();
  }
  if (auto status = state_->pacer.wait_idle(timeout); !status.ok()) {
    return status;
  }
  // This also abandons an image acquired by an unsuccessful recording. RHI
  // owns native retirement; the pacer protects resources of submitted frames.
  state_->swapchain = {};
  if (!state_->surface.valid()) {
    auto surface = state_->device->create_surface({
        .native = state_->desc.native,
        .initialExtent = state_->desc.extent,
        .debugName = "Renderer presentation surface",
    });
    if (!surface.ok()) {
      if (surface.status().code == base::StatusCode::surface_lost) {
        state_->surfaceStatus = surface.status();
      }
      return surface.status();
    }
    state_->surface = std::move(surface).value();
  }
  auto swapchain = state_->device->create_swapchain(
      state_->surface, {
                           .extent = state_->desc.extent,
                           .format = state_->desc.format,
                           .presentMode = state_->desc.presentMode,
                           .imageCount = state_->desc.imageCount,
                           .debugName = "Renderer presentation swapchain",
                       });
  if (!swapchain.ok()) {
    if (swapchain.status().code == base::StatusCode::surface_lost) {
      state_->surfaceStatus = swapchain.status();
    }
    return swapchain.status();
  }
  state_->swapchain = std::move(swapchain).value();
  state_->rebuild = false;
  return base::Status::success();
}

base::Result<PresentationFrame>
Presentation::begin_frame(std::chrono::nanoseconds timeout) {
  if (!valid() || state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Presentation: begin requires a valid object with no open frame");
  }
  state_->stats = {};
  if (state_->device->lost()) {
    return invalid(base::StatusCode::device_lost, "Presentation: device is lost");
  }
  if (!state_->surfaceStatus.ok()) {
    return state_->surfaceStatus;
  }
  if (suspended(state_->desc.extent)) {
    return invalid(base::StatusCode::out_of_date,
                   "Presentation: zero-sized surface is suspended until host resize");
  }
  if (auto status = prepare_surface(timeout); !status.ok()) {
    return status;
  }
  auto slot = state_->pacer.begin_frame(timeout);
  if (!slot.ok()) {
    return slot.status();
  }
  state_->open = true;
  return PresentationFrame{slot.value(), state_->swapchain.desc().extent};
}

base::Status Presentation::render(Renderer &renderer, const FrameGraph &graph) {
  if (!valid() || !state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Presentation: begin a frame before rendering");
  }
  if (&renderer.device() != state_->device) {
    (void)cancel_frame();
    return invalid(base::StatusCode::invalid_argument,
                   "Presentation: Renderer must share the same device object");
  }
  const bool presents =
      std::any_of(graph.nodes().begin(), graph.nodes().end(), [](const auto &node) {
        return node != nullptr && node->kind() == FrameGraphNodeKind::Render &&
               static_cast<const RenderPassNode *>(node.get())->uses_swapchain();
      });
  if (!presents) {
    (void)cancel_frame();
    return invalid(base::StatusCode::invalid_argument,
                   "Presentation: frame graph needs a swapchain render pass");
  }
  auto status = renderer.render(graph, &state_->swapchain, state_->pacer.signal());
  state_->stats = renderer.last_frame_stats();
  state_->pacer.end_frame(state_->stats.submitted);
  state_->open = false;
  if (!status.ok() || state_->stats.presentationSuboptimal) {
    state_->rebuild = true;
    if (status.code == base::StatusCode::surface_lost) {
      state_->surfaceStatus = status;
    }
    if (status.ok()) {
      return invalid(base::StatusCode::suboptimal,
                     "Presentation: acquired suboptimal image; rebuild on next frame");
    }
  }
  return status;
}

base::Status Presentation::cancel_frame() {
  if (!valid() || !state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Presentation: no open frame to cancel");
  }
  state_->pacer.end_frame(false);
  state_->open = false;
  return base::Status::success();
}

base::Status Presentation::wait_idle(std::chrono::nanoseconds timeout) {
  if (!valid() || state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Presentation: idle wait requires a valid object with no open frame");
  }
  if (state_->device->lost()) {
    return invalid(base::StatusCode::device_lost, "Presentation: device is lost");
  }
  return state_->pacer.wait_idle(timeout);
}

} // namespace truffle::renderer

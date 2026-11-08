#include "graphics_state.hpp"

#include <cassert>
#include <cmath>

namespace truffle::gui {
namespace {
base::Status invalid() {
  return base::Status::failure(base::StatusCode::invalid_state,
                               "GUI Graphics is not initialized");
}
base::Status scale_status(PixelSize pixels, float scale) {
  return std::isfinite(scale) && scale > 0 &&
                 std::isfinite(static_cast<float>(pixels.width) / scale) &&
                 std::isfinite(static_cast<float>(pixels.height) / scale)
             ? base::Status::success()
             : base::Status::failure(
                   base::StatusCode::invalid_argument,
                   "GUI DPI scale must be positive and produce finite logical sizes");
}
[[maybe_unused]] renderer::Backend backend(GraphicsBackend value) {
  switch (value) {
  case GraphicsBackend::automatic:
    return renderer::Backend::automatic;
  case GraphicsBackend::metal:
    return renderer::Backend::metal;
  case GraphicsBackend::vulkan:
    return renderer::Backend::vulkan;
  }
  return static_cast<renderer::Backend>(-1);
}
renderer::PresentationDesc presentation_desc(const GraphicsDesc &desc) {
  return {.native = desc.window,
          .extent = desc.pixels,
          .framesInFlight = desc.frames_in_flight};
}
} // namespace

Graphics::~Graphics() = default;
Graphics::Graphics(Graphics &&) noexcept = default;
Graphics &Graphics::operator=(Graphics &&) noexcept = default;
base::Result<Graphics> Graphics::create(const GraphicsDesc &desc) {
  if (auto status = scale_status(desc.pixels, desc.dpi_scale); !status.ok())
    return status;
  if (desc.window.kind == WindowKind::headless)
    return base::Status::failure(base::StatusCode::unsupported,
                                 "GUI owned setup requires a host native window");
  if (desc.backend < GraphicsBackend::automatic ||
      desc.backend > GraphicsBackend::vulkan) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown GUI graphics backend");
  }
#if TFL_GUI_NATIVE_PRESENTATION
  auto presentation = renderer::create_native_presentation(
      presentation_desc(desc), {
                                   .backend = backend(desc.backend),
                                   .enableValidation = desc.enable_validation,
                               });
  if (!presentation.ok())
    return presentation.status();
  return Rendering::initialize(std::move(presentation).value(), desc);
#else
  return base::Status::failure(base::StatusCode::unsupported,
                               "GUI owned setup requires TFL_BUILD_RENDERER_PRESENTATION "
                               "and an enabled native backend");
#endif
}
base::Result<Graphics> Rendering::create(renderer::Device &device,
                                         const GraphicsDesc &desc) {
  if (auto status = scale_status(desc.pixels, desc.dpi_scale); !status.ok())
    return status;
  auto presentation = renderer::Presentation::create(device, presentation_desc(desc));
  if (!presentation.ok())
    return presentation.status();
  return initialize(std::move(presentation).value(), desc);
}
base::Result<Graphics> Rendering::initialize(renderer::Presentation presentation,
                                              const GraphicsDesc &desc) {
  Graphics result;
  result.state_ = std::make_shared<Graphics::State>();
  auto &state = *result.state_;
  state.desc = desc;
  state.presentation = std::move(presentation);
  auto &sharedDevice = state.presentation.device();
  state.programs = std::make_unique<renderer::PipelineCache>(sharedDevice);
  state.renderer =
      std::make_unique<renderer::Renderer>(sharedDevice, state.programs.get());
  auto painter =
      specialist::Painter::create(renderer::RhiInterop::native(sharedDevice),
                                  renderer::RhiInterop::native(*state.programs),
                                  {
                                      .material = gui_program,
                                      .color_format = rhi::TextureFormat::bgra8_unorm,
                                      .frames_in_flight = desc.frames_in_flight,
                                  });
  if (!painter.ok())
    return painter.status();
  state.painter = std::move(painter).value();
  return result;
}
bool Graphics::valid() const noexcept { return state_ != nullptr; }
base::Status Graphics::resize(Context &context, PixelSize pixels, float scale) {
  if (!valid())
    return invalid();
  if (auto status = scale_status(pixels, scale); !status.ok())
    return status;
  if (auto status = state_->presentation.resize(pixels); !status.ok())
    return status;
  state_->desc.pixels = pixels;
  state_->desc.dpi_scale = scale;
  return context.set_viewport({static_cast<float>(pixels.width) / scale,
                               static_cast<float>(pixels.height) / scale},
                              scale);
}
base::Status Graphics::replace_window(Context &context, WindowHandle window,
                                      PixelSize pixels, float scale) {
  if (!valid())
    return invalid();
  if (auto status = scale_status(pixels, scale); !status.ok())
    return status;
  if (auto status = state_->presentation.replace_surface(window, pixels); !status.ok())
    return status;
  state_->desc.window = window;
  return resize(context, pixels, scale);
}
bool Graphics::needs_frame(const Context &context,
                           Context::Clock::time_point now) const noexcept {
  return valid() && state_->desc.pixels.width != 0 && state_->desc.pixels.height != 0 &&
         context.needs_frame(now);
}
base::Status Graphics::draw(Context &context, Context::Clock::time_point now) {
  if (!valid())
    return invalid();
  if (state_->desc.pixels.width == 0 || state_->desc.pixels.height == 0) {
    return base::Status::failure(base::StatusCode::out_of_date,
                                 "GUI window is minimized");
  }
  if (!needs_frame(context, now))
    return base::Status::success();
  auto frame = Rendering::begin_frame(*this, context, now);
  if (!frame.ok())
    return frame.status();
  renderer::FrameGraph graph;
  auto pass = Rendering::append_gui_pass(
      *this, context, graph, frame.value(),
      {
          .colorAttachments = {{.clear = {state_->desc.clear.r, state_->desc.clear.g,
                                          state_->desc.clear.b, state_->desc.clear.a}}},
      });
  if (!pass.ok()) {
    auto cancelled = Rendering::cancel_frame(*this, context);
    return cancelled.ok() ? pass.status() : cancelled;
  }
  return Rendering::present(*this, context, graph);
}
GraphicsFrameStats Graphics::last_frame_stats() const {
  return valid() ? state_->stats : GraphicsFrameStats{};
}
base::Status Graphics::wait_idle(std::chrono::nanoseconds timeout) {
  return valid() ? state_->presentation.wait_idle(timeout) : invalid();
}
renderer::Device &Rendering::device(Graphics &graphics) {
  assert(graphics.valid());
  return graphics.state_->presentation.device();
}
renderer::PipelineCache &Rendering::programs(Graphics &graphics) {
  assert(graphics.valid());
  return *graphics.state_->programs;
}
renderer::Renderer &Rendering::renderer(Graphics &graphics) {
  assert(graphics.valid());
  return *graphics.state_->renderer;
}
renderer::Presentation &Rendering::presentation(Graphics &graphics) {
  assert(graphics.valid());
  return graphics.state_->presentation;
}
base::Result<RenderingFrame> Rendering::begin_frame(Graphics &graphics,
                                                    Context &context,
                                                    Context::Clock::time_point now) {
  if (!graphics.valid())
    return invalid();
  auto &state = *graphics.state_;
  if (state.frame_open)
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI frame is already open");
  state.stats = {};
  auto frame = state.presentation.begin_frame();
  if (!frame.ok())
    return frame.status();
  state.frame_open = true;
  state.frame_context = &context;
  state.stats.gui = context.prepare_frame(now);
  if (!state.stats.gui.status.ok()) {
    auto cancelled = cancel_frame(graphics, context);
    return cancelled.ok() ? state.stats.gui.status : cancelled;
  }
  return RenderingFrame{frame.value().slot, frame.value().extent};
}
base::Status Rendering::cancel_frame(Graphics &graphics, Context &context) {
  if (!graphics.valid())
    return invalid();
  auto &state = *graphics.state_;
  if (!state.frame_open || state.frame_context != &context)
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI frame does not belong to the supplied Context");
  auto status = state.presentation.cancel_frame();
  if (status.ok()) {
    state.frame_open = false;
    state.frame_context = nullptr;
    context.request_frame();
  }
  return status;
}
base::Status Rendering::present(Graphics &graphics, Context &context,
                                const renderer::FrameGraph &graph) {
  if (!graphics.valid())
    return invalid();
  auto &state = *graphics.state_;
  if (!state.frame_open || state.frame_context != &context)
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI frame does not belong to the supplied Context");
  auto status = state.presentation.render(*state.renderer, graph);
  state.frame_open = false;
  state.frame_context = nullptr;
  const auto stats = state.presentation.last_frame_stats();
  state.stats.submitted = stats.submitted;
  state.stats.presented = stats.presented;
  state.stats.suboptimal = stats.presentationSuboptimal;
  if (!status.ok())
    context.request_frame();
  return status;
}

} // namespace truffle::gui

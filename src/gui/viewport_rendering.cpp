#include "graphics_state.hpp"
#include "truffle/gui/specialist/viewport.hpp"

namespace truffle::gui {

struct Viewport::State {
  specialist::Viewport raw;
  Context *context = nullptr;
  std::shared_ptr<renderer::Device> device;
  std::weak_ptr<void> gui_owner;
  State(specialist::Viewport viewport, Context &context)
      : raw(std::move(viewport)), context(&context) {}
};

namespace {
base::Status invalid(const char *message) {
  return base::Status::failure(base::StatusCode::invalid_state, message);
}
struct TargetLifetime {
  std::shared_ptr<renderer::Device> device;
  std::shared_ptr<renderer::specialist::SampledTexture> target;
};
struct PassLifetime {
  std::vector<renderer::RenderBatch> batches;
  renderer::RenderPassDesc pass;
};
} // namespace

base::Result<Viewport> Viewport::create(Context &context, ElementId parent) {
  auto raw = specialist::Viewport::create(context, parent);
  if (!raw.ok())
    return raw.status();
  Viewport result;
  result.state_ = std::make_shared<State>(std::move(raw).value(), context);
  return result;
}
ElementId Viewport::element() const noexcept {
  return state_ ? state_->raw.element() : ElementId{};
}
bool Viewport::alive() const noexcept { return state_ && state_->raw.alive(); }
base::Result<ViewportMetrics> Viewport::metrics() const {
  if (!state_)
    return invalid("GUI Viewport is not created");
  return state_->raw.metrics();
}
base::Status Viewport::set_event_handler(ViewportEventHandler handler) {
  return state_ ? state_->raw.set_event_handler(std::move(handler))
                : invalid("GUI Viewport is not created");
}
base::Status Viewport::request_redraw() {
  return state_ ? state_->raw.request_redraw() : invalid("GUI Viewport is not created");
}
base::Result<ViewportFrame> Rendering::prepare_viewport(renderer::Device &device,
                                                        Viewport &viewport,
                                                        std::uint32_t slot,
                                                        const ViewportTargetDesc &desc) {
  if (!viewport.alive() || !device.valid())
    return invalid("Viewport requires a live element and device");
  if (desc.format < renderer::TextureFormat::r8_unorm ||
      desc.format > renderer::TextureFormat::rgba32_float) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown/non-color viewport format");
  }
  auto &state = *viewport.state_;
  const auto frames = desc.frames_in_flight == 0 ? 2 : desc.frames_in_flight;
  auto raw =
      state.raw.prepare_frame(renderer::RhiInterop::native(device), slot,
                              {
                                  .format = static_cast<rhi::TextureFormat>(desc.format),
                                  .frames_in_flight = frames,
                              });
  auto owned = std::make_shared<renderer::Device>(renderer::RhiInterop::share(device));
  if (state.raw.retain_device(renderer::RhiInterop::native(device), owned))
    state.device = std::move(owned);
  if (!raw.ok())
    return raw.status();
  ViewportFrame result{raw.value().metrics, {}, raw.value().redraw};
  if (raw.value().target) {
    auto owner = std::make_shared<TargetLifetime>(
        TargetLifetime{state.device, raw.value().target_owner});
    result.target = renderer::RhiInterop::texture(
        raw.value().target->texture(), &raw.value().target->view(), std::move(owner));
  }
  return result;
}
base::Result<ViewportFrame> Rendering::prepare_viewport(Graphics &graphics,
                                                        Context &context,
                                                        Viewport &viewport,
                                                        std::uint32_t slot,
                                                        const ViewportTargetDesc &desc) {
  if (!graphics.valid() || !viewport.alive())
    return invalid("GUI Graphics/Viewport is not live");
  if (viewport.state_->context != &context)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Viewport must belong to the supplied Context");
  const auto frames = graphics.state_->presentation.desc().framesInFlight;
  if (desc.frames_in_flight != 0 && desc.frames_in_flight != frames)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Viewport and GUI must share safe frame slots");
  auto requested = desc;
  requested.frames_in_flight = frames;
  auto frame = prepare_viewport(device(graphics), viewport, slot, requested);
  if (frame.ok()) {
    viewport.state_->gui_owner = graphics.state_;
  }
  return frame;
}
base::Result<renderer::FrameGraph::NodeId>
Rendering::append_viewport_pass(Viewport &viewport, renderer::FrameGraph &graph,
                                std::vector<renderer::RenderBatch> batches,
                                renderer::RenderPassDesc pass) {
  if (!viewport.state_)
    return invalid("GUI Viewport is not created");
  if (auto order = graph.resolve_execution_order(); !order.ok())
    return order.status();
  auto owner =
      std::make_shared<PassLifetime>(PassLifetime{std::move(batches), std::move(pass)});
  std::vector<renderer::specialist::RenderBatch> raw;
  for (const auto &batch : owner->batches)
    raw.push_back(renderer::RhiInterop::native(batch));
  auto node = viewport.state_->raw.append_render_pass(
      renderer::RhiInterop::native(graph), std::move(raw),
      renderer::RhiInterop::native(owner->pass));
  if (node.ok()) {
    graph.retain(std::move(owner));
    graph.retain(viewport.state_);
    graph.retain(viewport.state_->gui_owner.lock());
  }
  return node;
}
base::Status Rendering::display_viewport(Graphics &graphics, Context &context,
                                         Viewport &viewport) {
  if (!graphics.valid() || !viewport.alive())
    return invalid("GUI Graphics/Viewport is not live");
  if (viewport.state_->context != &context)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Viewport must belong to the supplied Context");
  auto status = viewport.state_->raw.display(graphics.state_->painter);
  if (status.ok()) {
    viewport.state_->gui_owner = graphics.state_;
    viewport.state_->raw.painter_owner(graphics.state_);
  }
  return status;
}
base::Status Rendering::composite_viewport(Viewport &viewport,
                                           renderer::FrameGraph &graph,
                                           renderer::FrameGraph::NodeId gui_pass) {
  if (!viewport.state_)
    return invalid("GUI Viewport is not created");
  if (auto order = graph.resolve_execution_order(); !order.ok())
    return order.status();
  auto status =
      viewport.state_->raw.composite(renderer::RhiInterop::native(graph), gui_pass);
  if (status.ok()) {
    graph.retain(viewport.state_);
    graph.retain(viewport.state_->gui_owner.lock());
  }
  return status;
}
base::Status Rendering::finish_viewport(Viewport &viewport, bool submitted) {
  if (!viewport.state_)
    return invalid("GUI Viewport is not created");
  return viewport.state_->raw.finish_frame(submitted);
}
base::Status Rendering::release_viewport(Viewport &viewport) {
  if (!viewport.state_)
    return invalid("GUI Viewport is not created");
  auto status = viewport.state_->raw.release_gpu();
  if (status.ok()) {
    viewport.state_->gui_owner.reset();
    viewport.state_->device.reset();
  }
  return status;
}

} // namespace truffle::gui

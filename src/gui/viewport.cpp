#include "truffle/gui/viewport.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace truffle::gui {
namespace {

base::Status invalid(base::StatusCode code, const char *message) {
  return base::Status::failure(code, message);
}

Rect intersect(Rect a, Rect b) {
  const float x = std::max(a.x, b.x);
  const float y = std::max(a.y, b.y);
  return {x, y, std::max(0.0F, std::min(a.x + a.width, b.x + b.width) - x),
          std::max(0.0F, std::min(a.y + a.height, b.y + b.height) - y)};
}

} // namespace

struct Viewport::State {
  Context *context = nullptr;
  ElementId element;
  ViewportEventHandler handler;
  rhi::Device *device = nullptr;
  specialist::Painter *painter = nullptr;
  TextureId texture;
  ViewportTargetDesc desc;
  std::vector<std::shared_ptr<renderer::specialist::SampledTexture>> slots;
  std::shared_ptr<renderer::specialist::SampledTexture> committed;
  std::shared_ptr<renderer::specialist::SampledTexture> pending;
  std::shared_ptr<renderer::specialist::SampledTexture> displayed;
  ViewportMetrics committedMetrics;
  ViewportMetrics pendingMetrics;
  std::uint64_t revision = 1;
  std::uint64_t committedRevision = 0;
  std::uint64_t pendingRevision = 0;
  renderer::specialist::FrameGraph *graph = nullptr;
  std::optional<renderer::specialist::FrameGraph::NodeId> appPass;
  bool redraw = false;
  bool linked = false;
  bool open = false;

  ~State() {
    const auto status = release();
    assert(status.ok());
    (void)status;
  }

  bool live() const noexcept { return context != nullptr && context->alive(element); }

  base::Status release() {
    if (painter != nullptr && texture.valid()) {
      if (auto status = painter->remove_texture(texture); !status.ok()) {
        return status;
      }
    }
    painter = nullptr;
    texture = {};
    displayed.reset();
    pending.reset();
    committed.reset();
    slots.clear();
    device = nullptr;
    committedRevision = 0;
    return base::Status::success();
  }

  void cleanup() {
    context = nullptr;
    handler = {};
    // An outstanding graph retains this state through the checked reference.
    // Finish it before retiring wrappers borrowed by its nodes/batches.
    if (!open) {
      const auto status = release();
      assert(status.ok()); // the texture ID is private to this viewport
      (void)status;
    }
  }
};

base::Result<Viewport> Viewport::create(Context &context, ElementId parent) {
  auto element = context.create(parent);
  if (!element.ok()) {
    return element.status();
  }
  auto state = std::make_shared<State>();
  state->context = &context;
  state->element = element.value();
  auto fail = [&](base::Status status) -> base::Result<Viewport> {
    (void)context.destroy(element.value());
    return status;
  };
  if (auto status = context.set_cleanup(state->element, [state] { state->cleanup(); });
      !status.ok()) {
    return fail(status);
  }
  if (auto status = context.set_decoration(state->element, {.fill = {1, 1, 1, 1}});
      !status.ok()) {
    return fail(status);
  }
  if (auto status = context.set_interaction(state->element, {.focusable = true});
      !status.ok()) {
    return fail(status);
  }
  std::weak_ptr<State> weak = state;
  auto status = context.set_event_handler(
      state->element, [weak](Context &owner, RoutedEvent &event) {
        auto state = weak.lock();
        if (!state || !state->live() || !state->handler) {
          return;
        }
        Viewport viewport(state);
        auto metrics = viewport.metrics();
        if (!metrics.ok()) {
          event.status = metrics.status();
          return;
        }
        ViewportEvent input{event, metrics.value(), {}, {}};
        const Point *position = nullptr;
        if (const auto *pointer = event.input<PointerEvent>()) {
          position = &pointer->position;
        } else if (const auto *wheel = event.input<WheelEvent>()) {
          position = &wheel->position;
        }
        if (position != nullptr) {
          input.local = Point{position->x - input.metrics.bounds.x,
                              position->y - input.metrics.bounds.y};
          input.pixels = Point{input.local->x * input.metrics.dpi_scale,
                               input.local->y * input.metrics.dpi_scale};
        }
        const auto handler = state->handler;
        handler(owner, input); // may remove this element or its ancestors
      });
  if (!status.ok()) {
    return fail(status);
  }
  return Viewport(std::move(state));
}

ElementId Viewport::element() const noexcept {
  return state_ ? state_->element : ElementId{};
}
bool Viewport::alive() const noexcept { return state_ && state_->live(); }

base::Result<ViewportMetrics> Viewport::metrics() const {
  if (!alive()) {
    return invalid(base::StatusCode::invalid_argument, "GUI viewport is stale");
  }
  auto bounds = state_->context->absolute_bounds(state_->element);
  auto scale = state_->context->scale(state_->element);
  if (!bounds.ok()) {
    return bounds.status();
  }
  if (!scale.ok()) {
    return scale.status();
  }
  ViewportMetrics metrics{bounds.value(), {0, 0}, scale.value(), false};
  Rect visible = metrics.bounds;
  bool bypassClips = false;
  auto child = state_->element;
  for (;;) {
    if (!state_->context->visible(child)) {
      return metrics;
    }
    bypassClips = bypassClips || state_->context->overlay(child);
    auto parent = state_->context->parent(child);
    if (!parent.ok()) {
      return parent.status();
    }
    if (!parent.value().valid()) {
      auto root = state_->context->bounds(child);
      if (!root.ok()) {
        return root.status();
      }
      visible = intersect(visible, {0, 0, root.value().width, root.value().height});
      break;
    }
    if (!bypassClips && state_->context->clips_children(parent.value())) {
      auto clip = state_->context->absolute_bounds(parent.value());
      if (!clip.ok()) {
        return clip.status();
      }
      visible = intersect(visible, clip.value());
    }
    child = parent.value();
  }
  if (visible.width <= 0 || visible.height <= 0) {
    return metrics;
  }
  const auto width =
      std::ceil(static_cast<double>(metrics.bounds.width) * metrics.dpi_scale);
  const auto height =
      std::ceil(static_cast<double>(metrics.bounds.height) * metrics.dpi_scale);
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
      width > std::numeric_limits<std::uint32_t>::max() ||
      height > std::numeric_limits<std::uint32_t>::max()) {
    return invalid(base::StatusCode::invalid_argument,
                   "GUI viewport pixel extent is outside the representable range");
  }
  metrics.pixels = {static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height)};
  metrics.drawable = true;
  return metrics;
}

base::Status Viewport::set_event_handler(ViewportEventHandler handler) {
  if (!alive()) {
    return invalid(base::StatusCode::invalid_argument, "GUI viewport is stale");
  }
  state_->handler = std::move(handler);
  return base::Status::success();
}

base::Status Viewport::request_redraw() {
  if (!alive()) {
    return invalid(base::StatusCode::invalid_argument, "GUI viewport is stale");
  }
  ++state_->revision;
  state_->context->request_frame();
  return base::Status::success();
}

base::Result<ViewportFrame> Viewport::prepare_frame(rhi::Device &device,
                                                    std::uint32_t slot,
                                                    const ViewportTargetDesc &desc) {
  if (!alive() || state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: prepare requires a live element with no outstanding frame");
  }
  if (!device.valid() || desc.frames_in_flight == 0 || slot >= desc.frames_in_flight) {
    return invalid(base::StatusCode::invalid_argument,
                   "Viewport: valid device and safe frame slot required");
  }
  if (device.lost()) {
    return invalid(base::StatusCode::device_lost, "Viewport: device is lost");
  }
  if (state_->device != nullptr && (state_->device != &device || state_->desc != desc)) {
    return invalid(
        base::StatusCode::invalid_argument,
        "Viewport: release GPU state before changing device or target configuration");
  }
  auto measured = metrics();
  if (!measured.ok()) {
    return measured.status();
  }
  state_->pendingMetrics = measured.value();
  state_->pendingRevision = state_->revision;
  state_->pending.reset();
  state_->graph = nullptr;
  state_->appPass.reset();
  state_->linked = false;
  state_->redraw = false;
  if (measured.value().drawable) {
    if (state_->device == nullptr) {
      state_->device = &device;
      state_->desc = desc;
      state_->slots.resize(desc.frames_in_flight);
    }
    state_->redraw = !state_->committed ||
                     state_->committedRevision != state_->revision ||
                     state_->committedMetrics.pixels != measured.value().pixels ||
                     state_->committedMetrics.dpi_scale != measured.value().dpi_scale;
    if (state_->redraw) {
      auto &target = state_->slots[slot];
      const auto pixels = measured.value().pixels;
      if (!target || target->texture().desc().extent !=
                         rhi::Extent3D{pixels.width, pixels.height, 1}) {
        auto created = renderer::specialist::SampledTexture::render_target(
            device, {
                        .extent = pixels,
                        .format = desc.format,
                        .debugName = "GUI viewport",
                    });
        if (!created.ok()) {
          return created.status();
        }
        target = std::make_shared<renderer::specialist::SampledTexture>(std::move(created).value());
      }
      state_->pending = target;
    } else {
      state_->pending = state_->committed;
    }
  }
  state_->open = true;
  return ViewportFrame{state_->pendingMetrics, state_->pending.get(), state_->redraw};
}

base::Result<renderer::specialist::FrameGraph::NodeId>
Viewport::append_render_pass(renderer::specialist::FrameGraph &graph,
                             std::vector<renderer::specialist::RenderBatch> batches,
                             rhi::RenderPassDesc pass) {
  if (!alive() || !state_->open || !state_->redraw || !state_->pending ||
      state_->appPass) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: prepare a drawable redraw before appending its pass");
  }
  if (pass.colorAttachments.size() > 1 ||
      (!pass.colorAttachments.empty() &&
       (pass.colorAttachments.front().texture != nullptr ||
        pass.colorAttachments.front().resolveTexture != nullptr))) {
    return invalid(base::StatusCode::invalid_argument,
                   "Viewport: pass color target is supplied by the viewport");
  }
  if (pass.colorAttachments.empty()) {
    pass.colorAttachments.emplace_back();
  }
  pass.extent = state_->pendingMetrics.pixels;
  pass.colorAttachments.front().texture = &state_->pending->texture();
  auto node =
      std::make_unique<renderer::specialist::RenderPassNode>(std::move(pass), std::move(batches));
  node->transition_before(state_->pending->to_attachment());
  state_->appPass = graph.add_node(std::move(node));
  graph.retain(state_);
  graph.retain(state_->pending);
  state_->graph = &graph;
  return *state_->appPass;
}

base::Status Viewport::display(specialist::Painter &painter) {
  if (!alive() || !state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: prepare a frame before display");
  }
  if (!state_->pendingMetrics.drawable) {
    return base::Status::success();
  }
  if (!painter.valid() || &painter.device() != state_->device ||
      painter.frames_in_flight() != state_->desc.frames_in_flight) {
    return invalid(base::StatusCode::invalid_argument,
                   "Viewport: Painter must share the device and safe frame slots");
  }
  if (state_->redraw && !state_->appPass) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: append application rendering before displaying its target");
  }
  if (state_->painter != nullptr && state_->painter != &painter) {
    return invalid(base::StatusCode::invalid_argument,
                   "Viewport: release GPU state before changing Painter");
  }
  if (!state_->texture.valid()) {
    auto texture = painter.add_texture(*state_->pending);
    if (!texture.ok()) {
      return texture.status();
    }
    state_->painter = &painter;
    state_->texture = texture.value();
  } else if (state_->displayed != state_->pending) {
    if (auto status = painter.replace_texture(state_->texture, *state_->pending);
        !status.ok()) {
      return status;
    }
  }
  state_->displayed = state_->pending;
  auto decoration = state_->context->decoration(state_->element);
  if (!decoration.ok()) {
    return decoration.status();
  }
  decoration.value().texture = state_->texture;
  return state_->context->set_decoration(state_->element, decoration.value());
}

base::Status Viewport::composite(renderer::specialist::FrameGraph &graph,
                                 renderer::specialist::FrameGraph::NodeId guiPass) {
  if (!alive() || !state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: prepare a frame before compositing");
  }
  if (state_->linked || guiPass >= graph.nodes().size() ||
      graph.nodes()[guiPass] == nullptr ||
      graph.nodes()[guiPass]->kind() != renderer::specialist::FrameGraphNodeKind::Render) {
    return invalid(
        base::StatusCode::invalid_argument,
        "Viewport: composite needs one GUI render pass in the application's graph");
  }
  if (state_->redraw) {
    if (!state_->appPass || state_->graph != &graph) {
      return invalid(base::StatusCode::invalid_argument,
                     "Viewport: application and GUI passes must share the graph");
    }
    if (auto status = graph.add_dependency(*state_->appPass, guiPass); !status.ok()) {
      return status;
    }
    auto *node = static_cast<renderer::specialist::RenderPassNode *>(graph.nodes()[guiPass].get());
    node->transition_before(state_->pending->to_shader_read());
  }
  graph.retain(state_);
  if (state_->pending) {
    graph.retain(state_->pending);
  }
  state_->graph = &graph;
  state_->linked = true;
  return base::Status::success();
}

base::Status Viewport::finish_frame(bool submitted) {
  if (!state_ || !state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: no prepared frame to finish");
  }
  if (submitted && state_->redraw && !state_->linked) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: submitted redraw requires its composite transition");
  }
  if (submitted && state_->redraw) {
    state_->committed = state_->pending;
    state_->committedMetrics = state_->pendingMetrics;
    state_->committedRevision = state_->pendingRevision;
  }
  state_->pending.reset();
  state_->graph = nullptr;
  state_->appPass.reset();
  state_->open = false;
  if (!state_->live()) {
    return state_->release();
  }
  if (!submitted && state_->redraw) {
    state_->context->request_frame();
  }
  return base::Status::success();
}

base::Status Viewport::release_gpu() {
  if (!alive() || state_->open) {
    return invalid(base::StatusCode::invalid_state,
                   "Viewport: release requires a live element with no outstanding graph");
  }
  auto decoration = state_->context->decoration(state_->element);
  if (!decoration.ok()) {
    return decoration.status();
  }
  decoration.value().texture = {};
  if (auto status = state_->context->set_decoration(state_->element, decoration.value());
      !status.ok()) {
    return status;
  }
  auto status = state_->release();
  if (status.ok()) {
    state_->context->request_frame();
  }
  return status;
}

} // namespace truffle::gui

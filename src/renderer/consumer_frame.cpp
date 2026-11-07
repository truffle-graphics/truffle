#include "consumer_convert.hpp"

#include <cassert>

namespace truffle::renderer {

specialist::RenderBatch RhiInterop::native(const RenderBatch &value) {
  specialist::RenderBatch result;
  result.layout.bindingModel =
      detail::enum_value<specialist::BindingModel>(value.layout.bindingModel);
  for (const auto &channel : value.layout.channels)
    result.layout.channels.push_back(
        {detail::enum_value<specialist::ChannelKind>(channel.kind), channel.binding,
         channel.offset, channel.stride});
  result.instanceCount = value.instanceCount;
  result.vertexCount = value.vertexCount;
  result.vertexElementCount = value.vertexElementCount;
  for (std::size_t index = 0; index < RenderBatch::kMaxBindings; ++index)
    result.bindings[index] = native(value.bindings[index]);
  result.kind = detail::enum_value<specialist::DrawKind>(value.kind);
  result.material = value.material;
  result.variantHash = value.variantHash;
  result.indexFormat = detail::enum_value<rhi::IndexFormat>(value.indexFormat);
  result.indexBuffer = native(value.indexBuffer);
  result.indirectBuffer = native(value.indirectBuffer);
  result.uniformBuffer = native(value.uniformBuffer);
  for (const auto &group : value.bindGroups)
    result.bindGroups.push_back(native(group));
  if (value.scissor)
    result.scissor = rhi::ScissorRect{value.scissor->x, value.scissor->y,
                                      value.scissor->width, value.scissor->height};
  return result;
}
RenderBatch RhiInterop::batch(const specialist::RenderBatch &value,
                              std::shared_ptr<void> owner) {
  RenderBatch result;
  result.layout.bindingModel =
      detail::enum_value<BindingModel>(value.layout.bindingModel);
  for (const auto &channel : value.layout.channels)
    result.layout.channels.push_back({detail::enum_value<ChannelKind>(channel.kind),
                                      channel.binding, channel.offset, channel.stride});
  result.instanceCount = value.instanceCount;
  result.vertexCount = value.vertexCount;
  result.vertexElementCount = value.vertexElementCount;
  for (std::size_t index = 0; index < RenderBatch::kMaxBindings; ++index) {
    if (value.bindings[index].buffer)
      result.bindings[index] = buffer(value.bindings[index], owner);
  }
  result.kind = detail::enum_value<DrawKind>(value.kind);
  result.material = value.material;
  result.variantHash = value.variantHash;
  result.indexFormat = detail::enum_value<IndexFormat>(value.indexFormat);
  if (value.indexBuffer.buffer)
    result.indexBuffer = buffer(value.indexBuffer, owner);
  if (value.indirectBuffer.buffer)
    result.indirectBuffer = buffer(value.indirectBuffer, owner);
  if (value.uniformBuffer.buffer)
    result.uniformBuffer = buffer(value.uniformBuffer, owner);
  for (const auto &group : value.bindGroups)
    result.bindGroups.push_back(bindings(group, owner));
  if (value.scissor)
    result.scissor = PixelRect{value.scissor->x, value.scissor->y, value.scissor->width,
                               value.scissor->height};
  return result;
}
rhi::RenderPassDesc RhiInterop::native(const RenderPassDesc &value) {
  rhi::RenderPassDesc result{.extent = detail::extent(value.extent)};
  for (const auto &color : value.colorAttachments)
    result.colorAttachments.push_back({
        .texture = native(color.texture),
        .resolveTexture = native(color.resolveTexture),
        .loadOp = detail::enum_value<rhi::LoadOp>(color.loadOp),
        .storeOp = detail::enum_value<rhi::StoreOp>(color.storeOp),
        .clear = {color.clear.r, color.clear.g, color.clear.b, color.clear.a},
    });
  const auto &depth = value.depthStencilAttachment;
  result.depthStencilAttachment = {
      .texture = native(depth.texture),
      .depthLoadOp = detail::enum_value<rhi::LoadOp>(depth.depthLoadOp),
      .depthStoreOp = detail::enum_value<rhi::StoreOp>(depth.depthStoreOp),
      .clearDepth = depth.clearDepth,
      .stencilLoadOp = detail::enum_value<rhi::LoadOp>(depth.stencilLoadOp),
      .stencilStoreOp = detail::enum_value<rhi::StoreOp>(depth.stencilStoreOp),
      .clearStencil = depth.clearStencil,
  };
  return result;
}

struct FrameGraph::State {
  specialist::FrameGraph raw;
};
FrameGraph::FrameGraph() : state_(std::make_unique<State>()) {}
FrameGraph::~FrameGraph() = default;
FrameGraph::FrameGraph(FrameGraph &&) noexcept = default;
FrameGraph &FrameGraph::operator=(FrameGraph &&) noexcept = default;
specialist::FrameGraph &RhiInterop::native(const FrameGraph &value) {
  assert(value.state_);
  return value.state_->raw;
}

namespace {
base::Result<rhi::TextureBarrier> barrier(const TextureTransition &value) {
  if (!value.texture.valid() || value.before == value.after)
    return base::Status::failure(
        base::StatusCode::invalid_argument,
        "Texture transition requires an image and distinct uses");
  rhi::TextureBarrier result{.texture = RhiInterop::native(value.texture)};
  const auto use = [&](TextureUse use, bool source) -> bool {
    rhi::TextureLayout layout;
    rhi::PipelineStage stages;
    rhi::Access access;
    switch (use) {
    case TextureUse::sampled:
      layout = rhi::TextureLayout::shader_read_only;
      stages = rhi::PipelineStage::all_graphics;
      access = rhi::Access::shader_read;
      break;
    case TextureUse::color_attachment:
      layout = rhi::TextureLayout::color_attachment;
      stages = rhi::PipelineStage::color_attachment_output;
      access = rhi::Access::color_attachment_write;
      break;
    case TextureUse::depth_attachment:
      layout = rhi::TextureLayout::depth_stencil_attachment;
      stages = rhi::PipelineStage::early_fragment_tests |
               rhi::PipelineStage::late_fragment_tests;
      access = rhi::Access::depth_stencil_read | rhi::Access::depth_stencil_write;
      result.range.aspects = rhi::TextureAspect::depth;
      break;
    default:
      return false;
    }
    if (source) {
      result.oldLayout = layout;
      result.sourceStages = stages;
      result.sourceAccess = access;
    } else {
      result.newLayout = layout;
      result.destinationStages = stages;
      result.destinationAccess = access;
    }
    return true;
  };
  if (!use(value.before, true) || !use(value.after, false))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown texture use");
  return result;
}
} // namespace

base::Result<FrameGraph::NodeId> FrameGraph::add_render_pass(RenderPassNode value) {
  if (!state_)
    return detail::invalid("Frame graph was moved");
  auto owner = std::make_shared<RenderPassNode>(std::move(value));
  std::vector<specialist::RenderBatch> batches;
  for (const auto &batch : owner->batches)
    batches.push_back(RhiInterop::native(batch));
  auto node = std::make_unique<specialist::RenderPassNode>(
      RhiInterop::native(owner->pass), std::move(batches), owner->useWindow);
  for (const auto &transition : owner->transitions) {
    auto converted = barrier(transition);
    if (!converted.ok())
      return converted.status();
    node->transition_before(converted.value());
  }
  const auto id = state_->raw.add_node(std::move(node));
  state_->raw.retain(owner);
  return id;
}
base::Status FrameGraph::add_dependency(NodeId before, NodeId after) {
  return state_ ? state_->raw.add_dependency(before, after)
                : detail::invalid("Frame graph was moved");
}
base::Status FrameGraph::add_resource_usage(NodeId node, NodeResourceUsage usage) {
  return state_
             ? state_->raw.add_resource_usage(
                   node, {usage.resourceId,
                          detail::enum_value<specialist::ResourceAccess>(usage.access)})
             : detail::invalid("Frame graph was moved");
}
base::Result<std::vector<FrameGraph::NodeId>>
FrameGraph::resolve_execution_order() const {
  if (!state_)
    return detail::invalid("Frame graph was moved");
  return state_->raw.resolve_execution_order();
}
void FrameGraph::retain(std::shared_ptr<void> value) {
  assert(state_);
  state_->raw.retain(std::move(value));
}

struct Renderer::State {
  Device device;
  std::shared_ptr<void> programOwner;
  RendererFrameStats stats;
  specialist::Renderer raw;
  State(Device &device, PipelineCache *programs)
      : device(RhiInterop::share(device)),
        programOwner(programs ? RhiInterop::lifetime(*programs) : nullptr),
        raw(RhiInterop::native(device),
            programs ? &RhiInterop::native(*programs) : nullptr) {}
};
Renderer::Renderer(Device &device, PipelineCache *programs)
    : state_(device.valid() && (!programs || RhiInterop::same_device(*programs, device))
                 ? std::make_unique<State>(device, programs)
                 : nullptr) {}
Renderer::~Renderer() = default;
bool Renderer::valid() const noexcept { return state_ != nullptr; }
specialist::Renderer &RhiInterop::native(Renderer &value) {
  assert(value.state_);
  return value.state_->raw;
}
Device &Renderer::device() noexcept {
  assert(state_);
  return state_->device;
}
base::Status Renderer::render(const FrameGraph &graph, RenderSignal signal) {
  if (!state_)
    return detail::invalid("Renderer requires a valid device and a cache on that device");
  set_frame_stats({});
  if (auto order = graph.resolve_execution_order(); !order.ok())
    return order.status();
  auto status =
      state_->raw.render(RhiInterop::native(graph), nullptr, RhiInterop::native(signal));
  set_frame_stats(detail::stats(state_->raw.last_frame_stats()));
  return status;
}
void Renderer::set_frame_stats(RendererFrameStats stats) {
  if (state_)
    state_->stats = stats;
}
RendererFrameStats Renderer::last_frame_stats() const noexcept {
  return state_ ? state_->stats : RendererFrameStats{};
}
struct RenderSignal::State {
  specialist::RenderSignal raw;
  std::shared_ptr<void> owner;
};
RenderSignal RhiInterop::signal(specialist::RenderSignal raw,
                                std::shared_ptr<void> owner) {
  RenderSignal result;
  result.state_ =
      std::make_shared<RenderSignal::State>(RenderSignal::State{raw, std::move(owner)});
  return result;
}
specialist::RenderSignal RhiInterop::native(const RenderSignal &value) {
  return value.state_ ? value.state_->raw : specialist::RenderSignal{};
}
struct FramePacer::State {
  Device device;
  specialist::FramePacer raw;
};
base::Result<FramePacer> FramePacer::create(Device &device, std::uint32_t frames) {
  if (!device.valid())
    return detail::invalid("Pacer requires a device");
  auto raw = specialist::FramePacer::create(RhiInterop::native(device), frames);
  if (!raw.ok())
    return raw.status();
  FramePacer result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  return result;
}
base::Result<std::uint32_t> FramePacer::begin_frame(std::chrono::nanoseconds timeout) {
  if (!state_)
    return detail::invalid("Pacer is not created");
  return state_->raw.begin_frame(timeout);
}
RenderSignal FramePacer::signal() {
  return state_ ? RhiInterop::signal(state_->raw.signal(), state_) : RenderSignal{};
}
void FramePacer::end_frame(bool submitted) noexcept {
  if (state_)
    state_->raw.end_frame(submitted);
}
base::Status FramePacer::wait_idle(std::chrono::nanoseconds timeout) {
  return state_ ? state_->raw.wait_idle(timeout)
                : detail::invalid("Pacer is not created");
}

} // namespace truffle::renderer

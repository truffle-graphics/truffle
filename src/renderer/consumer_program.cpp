#include "consumer_convert.hpp"

#include <algorithm>
#include <cassert>
#include <map>

namespace truffle::renderer {

struct ProgramLayout::State {
  Device device;
  std::vector<BindingGroupDesc> declarations;
  std::vector<rhi::BindGroupLayout> groups;
  rhi::PipelineLayout pipeline;
};
struct ResourceBindings::State {
  Device device;
  std::shared_ptr<void> owner;
  std::vector<BindingValue> values;
  std::vector<rhi::Sampler> samplers;
  rhi::DescriptorArena arena;
  rhi::BindGroup owned;
  specialist::BatchBindGroup raw;
};
struct ShaderPackage::State {
  rhi::ShaderPackage raw;
};
struct PipelineCache::State {
  Device device;
  struct ProgramOwners {
    ShaderPackage vertex, fragment;
    ProgramLayout layout;
  };
  std::map<std::pair<MaterialId, std::size_t>, ProgramOwners> owners;
  specialist::PipelineCache raw;
  explicit State(Device &device)
      : device(RhiInterop::share(device)), raw(RhiInterop::native(device)) {}
};

namespace {
rhi::BindingType binding_type(ResourceKind kind) {
  switch (kind) {
  case ResourceKind::uniform_buffer:
    return rhi::BindingType::uniform_buffer;
  case ResourceKind::storage_buffer:
    return rhi::BindingType::storage_buffer;
  case ResourceKind::sampled_texture:
    return rhi::BindingType::sampled_texture;
  case ResourceKind::sampler:
    return rhi::BindingType::sampler;
  }
  return static_cast<rhi::BindingType>(-1);
}
rhi::ShaderStageMask visibility(ShaderVisibility stages) {
  switch (stages) {
  case ShaderVisibility::vertex:
    return rhi::ShaderStageMask::vertex;
  case ShaderVisibility::fragment:
    return rhi::ShaderStageMask::fragment;
  case ShaderVisibility::graphics:
    return rhi::ShaderStageMask::all_graphics;
  }
  return rhi::ShaderStageMask::none;
}
rhi::BlendComponent blend(const BlendComponent &value) {
  return {detail::enum_value<rhi::BlendFactor>(value.sourceFactor),
          detail::enum_value<rhi::BlendFactor>(value.destinationFactor),
          detail::enum_value<rhi::BlendOp>(value.operation)};
}
rhi::StencilFaceState stencil(const StencilFaceState &value) {
  return {detail::enum_value<rhi::CompareOp>(value.compare),
          detail::enum_value<rhi::StencilOp>(value.failOp),
          detail::enum_value<rhi::StencilOp>(value.depthFailOp),
          detail::enum_value<rhi::StencilOp>(value.passOp)};
}
rhi::PipelineDesc pipeline(const PipelineState &value) {
  rhi::PipelineDesc result;
  result.layout = value.layout ? RhiInterop::native(*value.layout) : nullptr;
  for (const auto &buffer : value.vertexBuffers) {
    rhi::VertexBufferLayoutDesc native{
        .stride = buffer.stride,
        .stepMode = detail::enum_value<rhi::VertexStepMode>(buffer.stepMode)};
    for (const auto &attribute : buffer.attributes)
      native.attributes.push_back(
          {attribute.location, detail::enum_value<rhi::VertexFormat>(attribute.format),
           attribute.offset});
    result.vertexBuffers.push_back(std::move(native));
  }
  for (const auto &color : value.colorTargets)
    result.colorTargets.push_back({
        .format = detail::enum_value<rhi::TextureFormat>(color.format),
        .blend = {color.blend.enabled, blend(color.blend.color),
                  blend(color.blend.alpha)},
        .writeMask = color.writeMask,
    });
  result.depthStencil = {
      .format = detail::enum_value<rhi::TextureFormat>(value.depthStencil.format),
      .depthWriteEnabled = value.depthStencil.depthWriteEnabled,
      .depthCompare = detail::enum_value<rhi::CompareOp>(value.depthStencil.depthCompare),
      .front = stencil(value.depthStencil.front),
      .back = stencil(value.depthStencil.back),
      .stencilReadMask = value.depthStencil.stencilReadMask,
      .stencilWriteMask = value.depthStencil.stencilWriteMask,
  };
  result.rasterization = {
      .polygonMode =
          detail::enum_value<rhi::PolygonMode>(value.rasterization.polygonMode),
      .cullMode = detail::enum_value<rhi::CullMode>(value.rasterization.cullMode),
      .frontFace = detail::enum_value<rhi::FrontFace>(value.rasterization.frontFace),
      .depthClampEnabled = value.rasterization.depthClampEnabled,
      .depthBias = value.rasterization.depthBias,
      .depthBiasSlopeScale = value.rasterization.depthBiasSlopeScale,
      .depthBiasClamp = value.rasterization.depthBiasClamp,
  };
  result.multisample = {value.multisample.sampleCount, value.multisample.sampleMask,
                        value.multisample.alphaToCoverageEnabled};
  result.topology = detail::enum_value<rhi::PrimitiveTopology>(value.topology);
  result.dynamicState =
      value.dynamicScissor ? rhi::DynamicState::scissor : rhi::DynamicState::none;
  result.debugName = value.debugName;
  return result;
}
rhi::ShaderTarget target(rhi::BackendKind backend, const ShaderSelection &selection,
                         rhi::ShaderStage stage) {
  switch (backend) {
  case rhi::BackendKind::metal: {
    const auto &variants = RhiInterop::native(*selection.package)->desc().variants;
    const bool hasLibrary =
        std::any_of(variants.begin(), variants.end(), [&](const auto &variant) {
          return variant.target == rhi::ShaderTarget::metal_library &&
                 variant.stage == stage && variant.entryPoint == selection.entryPoint &&
                 variant.permutation == selection.permutation;
        });
    return hasLibrary ? rhi::ShaderTarget::metal_library
                      : rhi::ShaderTarget::metal_source;
  }
  case rhi::BackendKind::direct3d12:
    return rhi::ShaderTarget::dxil;
  case rhi::BackendKind::opengl:
    return rhi::ShaderTarget::glsl;
  case rhi::BackendKind::opengles:
  case rhi::BackendKind::webgl2:
    return rhi::ShaderTarget::glsl_es;
  case rhi::BackendKind::webgpu:
    return rhi::ShaderTarget::wgsl;
  case rhi::BackendKind::null_validation:
    for (const auto &variant : RhiInterop::native(*selection.package)->desc().variants) {
      if (variant.stage == stage && variant.entryPoint == selection.entryPoint &&
          variant.permutation == selection.permutation)
        return variant.target;
    }
    return rhi::ShaderTarget::spirv;
  case rhi::BackendKind::vulkan:
    return rhi::ShaderTarget::spirv;
  }
  return static_cast<rhi::ShaderTarget>(255);
}
} // namespace

base::Result<ProgramLayout>
ProgramLayout::create(Device &device, std::span<const BindingGroupDesc> groups) {
  if (!device.valid())
    return detail::invalid("Program layout requires a device");
  ProgramLayout result;
  result.state_ = std::make_shared<State>();
  auto &state = *result.state_;
  state.device = RhiInterop::share(device);
  state.declarations.assign(groups.begin(), groups.end());
  state.groups.reserve(groups.size());
  for (const auto &group : groups) {
    rhi::BindGroupLayoutDesc desc{.group = group.group};
    for (const auto &slot : group.slots) {
      if (slot.kind < ResourceKind::uniform_buffer || slot.kind > ResourceKind::sampler ||
          slot.visibility < ShaderVisibility::vertex ||
          slot.visibility > ShaderVisibility::graphics) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Unknown Renderer binding kind/visibility");
      }
      desc.entries.push_back({
          .binding = slot.binding,
          .type = binding_type(slot.kind),
          .arrayCount = slot.arrayCount,
          .visibility = visibility(slot.visibility),
          .dynamicOffset = slot.dynamicOffset,
          .minimumBufferSize = slot.minimumBufferSize,
      });
    }
    auto raw = RhiInterop::native(device).create_bind_group_layout(desc);
    if (!raw.ok())
      return raw.status();
    state.groups.push_back(std::move(raw).value());
  }
  rhi::PipelineLayoutDesc desc;
  for (auto &group : state.groups)
    desc.bindGroupLayouts.push_back(&group);
  auto raw = RhiInterop::native(device).create_pipeline_layout(desc);
  if (!raw.ok())
    return raw.status();
  state.pipeline = std::move(raw).value();
  return result;
}
base::Result<ProgramLayout> ProgramLayout::sampled_texture(Device &device) {
  const std::array<BindingGroupDesc, 1> desc{
      {{0,
        {
            {.binding = 0,
             .kind = ResourceKind::sampled_texture,
             .visibility = ShaderVisibility::fragment},
            {.binding = 1,
             .kind = ResourceKind::sampler,
             .visibility = ShaderVisibility::fragment},
        }}}};
  return create(device, desc);
}
base::Result<ResourceBindings>
ProgramLayout::bind(Device &device, std::uint32_t group,
                    std::span<const BindingValue> values,
                    std::vector<std::uint32_t> dynamicOffsets) const {
  if (!state_ || !device.valid() ||
      &RhiInterop::native(device) != &RhiInterop::native(state_->device)) {
    return detail::invalid("Bindings must share their layout's device");
  }
  const auto declaration =
      std::find_if(state_->declarations.begin(), state_->declarations.end(),
                   [&](const auto &candidate) { return candidate.group == group; });
  if (declaration == state_->declarations.end())
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Bindings reference an undeclared group");
  const auto groupIndex =
      static_cast<std::size_t>(declaration - state_->declarations.begin());
  ResourceBindings result;
  result.state_ = std::make_shared<ResourceBindings::State>();
  auto &bindings = *result.state_;
  bindings.device = RhiInterop::share(device);
  bindings.owner = state_;
  bindings.values.assign(values.begin(), values.end());
  bindings.samplers.reserve(values.size());
  auto arena = RhiInterop::native(device).create_descriptor_arena();
  if (!arena.ok())
    return arena.status();
  bindings.arena = std::move(arena).value();
  rhi::BindGroupDesc desc{.layout = &state_->groups[groupIndex],
                          .arena = &bindings.arena};
  for (const auto &value : values) {
    const auto slot = std::find_if(
        declaration->slots.begin(), declaration->slots.end(),
        [&](const auto &candidate) { return candidate.binding == value.binding; });
    if (slot == declaration->slots.end())
      return base::Status::failure(base::StatusCode::invalid_argument,
                                   "Bindings reference an undeclared slot");
    rhi::BindGroupEntry entry{.binding = value.binding,
                              .arrayElement = value.arrayElement};
    switch (slot->kind) {
    case ResourceKind::uniform_buffer:
    case ResourceKind::storage_buffer: {
      const auto buffer = RhiInterop::native(value.buffer);
      entry.buffer = buffer.buffer;
      entry.offset = buffer.offset;
      entry.size = buffer.size;
      break;
    }
    case ResourceKind::sampled_texture:
      entry.textureView = RhiInterop::view(value.texture);
      break;
    case ResourceKind::sampler: {
      auto sampler =
          RhiInterop::native(device).create_sampler(detail::sampler(value.sampler));
      if (!sampler.ok())
        return sampler.status();
      bindings.samplers.push_back(std::move(sampler).value());
      entry.sampler = &bindings.samplers.back();
      break;
    }
    }
    desc.entries.push_back(entry);
  }
  auto raw = RhiInterop::native(device).create_bind_group(desc);
  if (!raw.ok())
    return raw.status();
  bindings.owned = std::move(raw).value();
  bindings.raw = {group, &bindings.owned, std::move(dynamicOffsets)};
  return result;
}
rhi::PipelineLayout *RhiInterop::native(const ProgramLayout &value) {
  return value.state_ ? &value.state_->pipeline : nullptr;
}
bool ResourceBindings::valid() const noexcept {
  return state_ && state_->raw.bindings && state_->raw.bindings->valid();
}
specialist::BatchBindGroup RhiInterop::native(const ResourceBindings &value) {
  return value.state_ ? value.state_->raw : specialist::BatchBindGroup{};
}
ResourceBindings RhiInterop::bindings(specialist::BatchBindGroup raw,
                                      std::shared_ptr<void> owner) {
  ResourceBindings result;
  result.state_ = std::make_shared<ResourceBindings::State>();
  result.state_->owner = std::move(owner);
  result.state_->raw = std::move(raw);
  return result;
}
base::Result<ShaderPackage> ShaderPackage::load(std::span<const std::byte> bytes) {
  auto raw = rhi::ShaderPackage::load(bytes);
  if (!raw.ok())
    return raw.status();
  ShaderPackage result;
  result.state_ = std::make_shared<State>();
  result.state_->raw = std::move(raw).value();
  return result;
}
rhi::ShaderPackage *RhiInterop::native(const ShaderPackage &value) {
  return value.state_ ? &value.state_->raw : nullptr;
}
PipelineCache::PipelineCache(Device &device)
    : state_(device.valid() ? std::make_shared<State>(device) : nullptr) {}
PipelineCache::~PipelineCache() = default;
specialist::PipelineCache &RhiInterop::native(PipelineCache &value) {
  assert(value.state_);
  return value.state_->raw;
}
std::shared_ptr<void> RhiInterop::lifetime(PipelineCache &value) { return value.state_; }
bool RhiInterop::same_device(const PipelineCache &value, Device &device) {
  return value.state_ && device.valid() &&
         &native(value.state_->device) == &native(device);
}
bool PipelineCache::valid() const noexcept { return state_ != nullptr; }
base::Status PipelineCache::register_program(MaterialId material, const ProgramDesc &desc,
                                             std::size_t variant) {
  if (!state_)
    return detail::invalid("Program cache requires a valid device");
  if (!desc.vertex.package || !desc.fragment.package ||
      !RhiInterop::native(*desc.vertex.package) ||
      !RhiInterop::native(*desc.fragment.package) ||
      (desc.pipeline.layout && !RhiInterop::native(*desc.pipeline.layout))) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Program requires valid packages/layout");
  }
  for (const auto &color : desc.pipeline.colorTargets) {
    if (!detail::valid_format(color.format))
      return base::Status::failure(base::StatusCode::invalid_argument,
                                   "Unknown Renderer color format");
  }
  if (!detail::valid_format(desc.pipeline.depthStencil.format))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer depth format");
  if (desc.pipeline.layout && &RhiInterop::native(desc.pipeline.layout->state_->device) !=
                                  &RhiInterop::native(state_->device))
    return detail::invalid("Program layout must share the cache's device");
  const auto backend = RhiInterop::native(state_->device).adapter_info().backend;
  specialist::ProgramDesc raw;
  raw.vertex = {RhiInterop::native(*desc.vertex.package),
                target(backend, desc.vertex, rhi::ShaderStage::vertex),
                desc.vertex.entryPoint, rhi::ShaderStage::vertex,
                desc.vertex.permutation};
  raw.fragment = {RhiInterop::native(*desc.fragment.package),
                  target(backend, desc.fragment, rhi::ShaderStage::fragment),
                  desc.fragment.entryPoint, rhi::ShaderStage::fragment,
                  desc.fragment.permutation};
  raw.pipeline = pipeline(desc.pipeline);
  auto status = state_->raw.register_program(material, raw, variant);
  if (status.ok())
    state_->owners.insert_or_assign(
        std::pair{material, variant},
        State::ProgramOwners{
            *desc.vertex.package,
            *desc.fragment.package,
            desc.pipeline.layout ? *desc.pipeline.layout : ProgramLayout{},
        });
  return status;
}
bool PipelineCache::published(MaterialId material, std::size_t variant) const noexcept {
  return state_ && state_->raw.published(material, variant) != nullptr;
}
void PipelineCache::invalidate(MaterialId material) {
  if (state_) {
    state_->raw.invalidate(material);
    std::erase_if(state_->owners,
                  [&](const auto &entry) { return entry.first.first == material; });
  }
}
void PipelineCache::invalidate_all() {
  if (state_) {
    state_->raw.invalidate_all();
    state_->owners.clear();
  }
}

} // namespace truffle::renderer

#pragma once

#include "truffle/renderer/texture.hpp"

namespace truffle::renderer {

struct BindingSlot {
  std::uint32_t binding = 0;
  ResourceKind kind = ResourceKind::uniform_buffer;
  ShaderVisibility visibility = ShaderVisibility::graphics;
  std::uint32_t arrayCount = 1;
  std::size_t minimumBufferSize = 0;
  bool dynamicOffset = false;
};
struct BindingGroupDesc {
  std::uint32_t group = 0;
  std::vector<BindingSlot> slots;
};
struct BindingValue {
  std::uint32_t binding = 0, arrayElement = 0;
  BufferSlice buffer;
  Texture texture;
  SamplerDesc sampler;
};

class ProgramLayout {
public:
  ProgramLayout() = default;
  [[nodiscard]] static base::Result<ProgramLayout>
  create(Device &, std::span<const BindingGroupDesc>);
  [[nodiscard]] static base::Result<ProgramLayout> sampled_texture(Device &);
  [[nodiscard]] base::Result<ResourceBindings>
  bind(Device &, std::uint32_t group, std::span<const BindingValue>,
       std::vector<std::uint32_t> dynamicOffsets = {}) const;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
  friend class PipelineCache;
};

class ShaderPackage {
public:
  ShaderPackage() = default;
  [[nodiscard]] static base::Result<ShaderPackage> load(std::span<const std::byte>);

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};
struct ShaderSelection {
  const ShaderPackage *package = nullptr;
  std::string entryPoint = "main", permutation;
};
struct PipelineState {
  const ProgramLayout *layout = nullptr;
  std::vector<VertexBufferLayoutDesc> vertexBuffers;
  std::vector<ColorTargetState> colorTargets;
  DepthStencilState depthStencil;
  RasterizationState rasterization;
  MultisampleState multisample;
  PrimitiveTopology topology = PrimitiveTopology::triangle_list;
  bool dynamicScissor = false;
  std::string debugName;
};
struct ProgramDesc {
  ShaderSelection vertex, fragment;
  PipelineState pipeline;
};

class PipelineCache {
public:
  explicit PipelineCache(Device &);
  ~PipelineCache();
  PipelineCache(const PipelineCache &) = delete;
  PipelineCache &operator=(const PipelineCache &) = delete;
  [[nodiscard]] base::Status register_program(MaterialId, const ProgramDesc &,
                                              std::size_t variant = 0);
  [[nodiscard]] bool published(MaterialId, std::size_t variant = 0) const noexcept;
  [[nodiscard]] bool valid() const noexcept;
  void invalidate(MaterialId);
  void invalidate_all();

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

} // namespace truffle::renderer

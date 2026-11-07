#pragma once

// Explicit specialist opt-in. Ordinary Renderer headers never include this.
#include "truffle/renderer/presentation.hpp"
#include "truffle/renderer/specialist/presentation.hpp"
#include "truffle/renderer/specialist/texture.hpp"

namespace truffle::renderer {

struct RhiInterop {
  [[nodiscard]] static base::Result<Device> borrow(rhi::Device &);
  [[nodiscard]] static base::Result<Device> adopt(rhi::Instance, rhi::Device);
  [[nodiscard]] static Device share(Device &);
  [[nodiscard]] static rhi::Device &native(Device &);
  [[nodiscard]] static specialist::PipelineCache &native(PipelineCache &);
  [[nodiscard]] static std::shared_ptr<void> lifetime(PipelineCache &);
  [[nodiscard]] static bool same_device(const PipelineCache &, Device &);
  [[nodiscard]] static specialist::Renderer &native(Renderer &);
  [[nodiscard]] static specialist::FrameGraph &native(const FrameGraph &);
  [[nodiscard]] static specialist::Presentation &native(Presentation &);
  [[nodiscard]] static specialist::SampledTexture &native(SampledTexture &);
  [[nodiscard]] static rhi::Texture *native(const Texture &);
  [[nodiscard]] static rhi::TextureView *view(const Texture &);
  [[nodiscard]] static specialist::BufferView native(const BufferSlice &);
  [[nodiscard]] static specialist::BatchBindGroup native(const ResourceBindings &);
  [[nodiscard]] static rhi::PipelineLayout *native(const ProgramLayout &);
  [[nodiscard]] static rhi::ShaderPackage *native(const ShaderPackage &);
  [[nodiscard]] static specialist::RenderSignal native(const RenderSignal &);
  [[nodiscard]] static RenderSignal signal(specialist::RenderSignal,
                                           std::shared_ptr<void>);
  [[nodiscard]] static Texture texture(rhi::Texture &, rhi::TextureView * = nullptr,
                                       std::shared_ptr<void> owner = {});
  [[nodiscard]] static BufferSlice buffer(specialist::BufferView,
                                          std::shared_ptr<void> owner = {});
  [[nodiscard]] static ResourceBindings bindings(specialist::BatchBindGroup,
                                                 std::shared_ptr<void> owner = {});
  [[nodiscard]] static RenderBatch batch(const specialist::RenderBatch &,
                                         std::shared_ptr<void> owner = {});
  [[nodiscard]] static specialist::RenderBatch native(const RenderBatch &);
  [[nodiscard]] static rhi::RenderPassDesc native(const RenderPassDesc &);
};

} // namespace truffle::renderer

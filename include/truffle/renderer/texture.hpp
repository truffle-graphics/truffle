#pragma once

#include "truffle/renderer/render_batch.hpp"
#include "truffle/rhi/rhi.hpp"

#include <span>
#include <string>

namespace truffle::renderer {

// Uncompressed color formats: r8/rg8/rgba8(srgb)/bgra8(srgb)/rgba16f/rgba32f.
struct TextureUpload {
    rhi::Extent2D extent{};
    rhi::TextureFormat format = rhi::TextureFormat::rgba8_unorm;
    std::span<const std::byte> bytes;
    std::size_t bytesPerRow = 0; // 0 = tightly packed rows
    std::string debugName = "Renderer sampled texture";
};

// Device-local 2D texture sampled by shaders (for example a font atlas or
// icons), with one mip and layer. Uploads/updates copy bytes unchanged through a
// staging buffer and wait for the copy; the texture is left shader-readable.
// Updates follow earlier graphics-queue work in submission order. Views borrow
// this object's wrappers: keep it alive and unmoved while batches reference them.
class SampledTexture {
public:
    SampledTexture() = default;
    SampledTexture(SampledTexture&&) noexcept = default;
    SampledTexture& operator=(SampledTexture&&) noexcept = default;
    SampledTexture(const SampledTexture&) = delete;
    SampledTexture& operator=(const SampledTexture&) = delete;

    [[nodiscard]] static base::Result<SampledTexture> upload(rhi::Device& device,
                                                             const TextureUpload& desc);
    // Replaces a sub-rectangle; `bytesPerRow` 0 means tightly packed.
    [[nodiscard]] base::Status update(rhi::Device& device, rhi::Origin3D origin,
                                      rhi::Extent2D extent,
                                      std::span<const std::byte> bytes,
                                      std::size_t bytesPerRow = 0);
    [[nodiscard]] rhi::Texture& texture() noexcept { return texture_; }
    [[nodiscard]] rhi::TextureView& view() noexcept { return view_; }

private:
    rhi::Texture texture_;
    rhi::TextureView view_;
};

// Descriptor layout for one sampled texture (binding 0) and its sampler
// (binding 1) in a single group, plus a pipeline layout holding only that group
// for `rhi::PipelineDesc::layout`. Keep it alive while programs created with its
// pipeline layout are staged and while bindings created from it are used.
class TextureBindingLayout {
public:
    TextureBindingLayout() = default;
    TextureBindingLayout(TextureBindingLayout&&) noexcept = default;
    TextureBindingLayout& operator=(TextureBindingLayout&&) noexcept = default;
    TextureBindingLayout(const TextureBindingLayout&) = delete;
    TextureBindingLayout& operator=(const TextureBindingLayout&) = delete;

    [[nodiscard]] static base::Result<TextureBindingLayout> create(
        rhi::Device& device,
        rhi::ShaderStageMask visibility = rhi::ShaderStageMask::fragment);
    [[nodiscard]] rhi::BindGroupLayout& group_layout() noexcept { return group_; }
    [[nodiscard]] rhi::PipelineLayout& pipeline_layout() noexcept { return pipeline_; }

private:
    rhi::BindGroupLayout group_;
    rhi::PipelineLayout pipeline_;
};

// Owns a sampler and the bind group joining it with a SampledTexture under a
// TextureBindingLayout. The texture must outlive it. Batches borrow its group:
// keep it alive and unmoved while they reference it.
class TextureBinding {
public:
    TextureBinding() = default;
    TextureBinding(TextureBinding&&) noexcept = default;
    TextureBinding& operator=(TextureBinding&&) noexcept = default;
    TextureBinding(const TextureBinding&) = delete;
    TextureBinding& operator=(const TextureBinding&) = delete;

    [[nodiscard]] static base::Result<TextureBinding> create(
        rhi::Device& device, TextureBindingLayout& layout, SampledTexture& texture,
        const rhi::SamplerDesc& sampler = {});
    [[nodiscard]] rhi::BindGroup& group() noexcept { return group_; }
    // Ready to append to `RenderBatch::bindGroups` as group `index`.
    [[nodiscard]] BatchBindGroup batch_group(std::uint32_t index = 0) noexcept {
        return {.group = index, .bindings = &group_};
    }

private:
    rhi::Sampler sampler_;
    rhi::DescriptorArena arena_;
    rhi::BindGroup group_;
};

} // namespace truffle::renderer

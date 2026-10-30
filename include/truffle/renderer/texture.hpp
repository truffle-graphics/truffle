#pragma once

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

} // namespace truffle::renderer

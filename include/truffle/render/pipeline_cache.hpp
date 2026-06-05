#pragma once

#include "truffle/render/render_batch.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <unordered_map>

namespace truffle::render {

// ---------------------------------------------------------------------------
// ShaderBinding — vertex/fragment shader pair for one material
// ---------------------------------------------------------------------------

struct ShaderBinding {
    rhi::IShader* vertexShader   = nullptr;
    rhi::IShader* fragmentShader = nullptr;
};

// ---------------------------------------------------------------------------
// IPipelineCache — maps (InstanceLayout, MaterialId) → IPipeline
// ---------------------------------------------------------------------------

class IPipelineCache {
public:
    virtual ~IPipelineCache() = default;

    // Register vertex/fragment shaders for a material ID.
    // Must be called before the first get_or_create for that material.
    virtual void register_shaders(MaterialId material,
                                  const ShaderBinding& shaders) = 0;

    // Returns a matching pipeline or creates one.
    // variantHash can be used to bucket pipelines driven by shader variants.
    [[nodiscard]] virtual rhi::IPipeline* get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) = 0;

    // Invalidate caches to trigger hot-reloads
    virtual void invalidate(MaterialId material) = 0;
    virtual void invalidate_all() = 0;
};

// ---------------------------------------------------------------------------
// NullPipelineCache — placeholder; ignores layout/material, always returns
// a single null pipeline with no shaders (suitable for the null backend only).
// ---------------------------------------------------------------------------

class NullPipelineCache final : public IPipelineCache {
public:
    explicit NullPipelineCache(rhi::IDevice& device);

    void register_shaders(MaterialId /*material*/,
                          const ShaderBinding& /*shaders*/) override {}

    [[nodiscard]] rhi::IPipeline* get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) override;

    void invalidate(MaterialId /*material*/) override {}
    void invalidate_all() override {}

private:
    rhi::IDevice*                   device_   = nullptr;
    std::unique_ptr<rhi::IPipeline> pipeline_;
};

// ---------------------------------------------------------------------------
// PipelineCache — real cache keyed on (InstanceLayout hash, MaterialId).
// Requires register_shaders() before the first get_or_create for a material.
// ---------------------------------------------------------------------------

class PipelineCache final : public IPipelineCache {
public:
    explicit PipelineCache(rhi::IDevice& device,
                           rhi::TextureFormat colorFormat =
                               rhi::TextureFormat::bgra8_unorm);

    void register_shaders(MaterialId material,
                          const ShaderBinding& shaders) override;

    [[nodiscard]] rhi::IPipeline* get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) override;

    void invalidate(MaterialId material) override;
    void invalidate_all() override;

private:
    struct CacheKey {
        std::size_t layoutHash;
        MaterialId  material;
        std::size_t variantHash;
        bool operator==(const CacheKey& o) const noexcept {
            return layoutHash == o.layoutHash && material == o.material && variantHash == o.variantHash;
        }
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& k) const noexcept {
            return k.layoutHash ^ (k.material * 0x9e3779b9u) ^ (k.variantHash * 0x01000193u);
        }
    };

    rhi::IDevice*    device_;
    rhi::TextureFormat colorFormat_;
    std::unordered_map<MaterialId, ShaderBinding>                          shaders_;
    std::unordered_map<CacheKey, std::unique_ptr<rhi::IPipeline>, CacheKeyHash> pipelines_;
};

} // namespace truffle::render

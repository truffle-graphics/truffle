#pragma once

#include "truffle/renderer/render_batch.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace truffle::renderer {

// Ownership is transferred, never borrowed from caller-local shader wrappers.
struct ShaderBinding {
    rhi::Shader vertexShader;
    rhi::Shader fragmentShader;
};

struct ShaderSelection {
    const rhi::ShaderPackage* package = nullptr;
    rhi::ShaderTarget target = rhi::ShaderTarget::spirv;
    std::string entryPoint = "main";
    rhi::ShaderStage stage = rhi::ShaderStage::vertex;
    std::string permutation;
};

struct ProgramDesc {
    ShaderSelection vertex;
    ShaderSelection fragment{.stage = rhi::ShaderStage::fragment};
    // Shader pointers must be null. Layout/cache are borrowed only during staging;
    // the created RHI pipeline retains their payloads. Static graphics state only.
    rhi::PipelineDesc pipeline;
};

class PipelineCache;

// A complete, native-validated candidate. Destroying it discards the candidate
// without changing the published material. It retains no package/source pointers.
class StagedProgram {
public:
    StagedProgram(StagedProgram&&) noexcept = default;
    StagedProgram& operator=(StagedProgram&&) noexcept = default;
    StagedProgram(const StagedProgram&) = delete;
    StagedProgram& operator=(const StagedProgram&) = delete;

private:
    StagedProgram() = default;
    const PipelineCache* owner_ = nullptr;
    ShaderBinding shaders_;
    rhi::Pipeline pipeline_;
    rhi::ObjectId layoutId_{};
    std::vector<rhi::TextureFormat> colorFormats_;
    rhi::TextureFormat depthStencilFormat_ = rhi::TextureFormat::unknown;
    std::uint32_t sampleCount_ = 1;
    friend class PipelineCache;
};

// Caches, staging/publication and Renderer recording are single-thread-owned.
// Returned pipeline pointers are borrowed until replacement/invalidation/destruction.
class IPipelineCache {
public:
    virtual ~IPipelineCache() = default;
    [[nodiscard]] virtual base::Status prepare_render_pass(
        const rhi::RenderPassDesc& pass) = 0;
    [[nodiscard]] virtual base::Result<rhi::Pipeline*> get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) = 0;
    virtual void invalidate(MaterialId material) = 0;
    virtual void invalidate_all() = 0;
};

// Explicit validation-only cache: shaderless pipelines are not native programs.
class NullPipelineCache final : public IPipelineCache {
public:
    explicit NullPipelineCache(rhi::Device& device);
    [[nodiscard]] base::Status prepare_render_pass(
        const rhi::RenderPassDesc& pass) override;
    [[nodiscard]] base::Result<rhi::Pipeline*> get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) override;
    void invalidate(MaterialId /*material*/) override {}
    void invalidate_all() override { pipeline_ = {}; }

private:
    rhi::Device* device_ = nullptr;
    rhi::Pipeline pipeline_;
    std::vector<rhi::TextureFormat> colorFormats_;
    rhi::TextureFormat depthStencilFormat_ = rhi::TextureFormat::unknown;
    std::uint32_t sampleCount_ = 1;
};

// Each (material, variantHash) names one immutable shader + graphics-state
// configuration. Staging creates its pipeline eagerly; lookup reuses it.
// InstanceLayout describes data lanes, not implicit vertex-attribute semantics;
// native vertex input is specified explicitly in ProgramDesc::pipeline.
class PipelineCache final : public IPipelineCache {
public:
    explicit PipelineCache(rhi::Device& device);
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) = delete;
    PipelineCache& operator=(PipelineCache&&) = delete;

    [[nodiscard]] base::Result<StagedProgram> stage_program(
        const ProgramDesc& desc);
    [[nodiscard]] base::Result<StagedProgram> stage_shaders(
        ShaderBinding shaders, const rhi::PipelineDesc& state);
    [[nodiscard]] base::Status commit_program(
        MaterialId material, StagedProgram&& program, std::size_t variantHash = 0);
    [[nodiscard]] base::Status register_program(
        MaterialId material, const ProgramDesc& desc, std::size_t variantHash = 0);
    [[nodiscard]] base::Status register_shaders(
        MaterialId material, ShaderBinding shaders,
        const rhi::PipelineDesc& state, std::size_t variantHash = 0);

    [[nodiscard]] base::Status prepare_render_pass(
        const rhi::RenderPassDesc& pass) override;
    [[nodiscard]] base::Result<rhi::Pipeline*> get_or_create(
        const InstanceLayout& layout, MaterialId material,
        std::size_t variantHash = 0) override;
    // Published slot pipeline without render-pass checks, or null. Borrowed as above.
    [[nodiscard]] const rhi::Pipeline* published(
        MaterialId material, std::size_t variantHash = 0) const noexcept;

    // Removes complete programs; subsequent lookup fails until registration.
    void invalidate(MaterialId material) override;
    void invalidate_all() override;

private:
    struct CacheKey {
        MaterialId material;
        std::size_t variantHash;
        bool operator==(const CacheKey&) const = default;
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const noexcept {
            return std::hash<MaterialId>{}(key.material) ^
                   (std::hash<std::size_t>{}(key.variantHash) << 1u);
        }
    };
    rhi::Device* device_;
    bool passPrepared_ = false;
    std::vector<rhi::TextureFormat> colorFormats_;
    rhi::TextureFormat depthStencilFormat_ = rhi::TextureFormat::unknown;
    std::uint32_t sampleCount_ = 1;
    std::unordered_map<CacheKey, std::unique_ptr<StagedProgram>, CacheKeyHash> programs_;
};

} // namespace truffle::renderer

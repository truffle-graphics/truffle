#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace truffle::shader {

#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
rhi::Result<Compilation> compile_glslang(const CompileRequest& request,
                                       std::vector<Compilation>& cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
rhi::Result<Compilation> compile_dxc(const CompileRequest& request,
                                   std::vector<Compilation>& cache);
#endif

struct Compiler::Impl {
    std::vector<Compilation> cache;
};

Compiler::Compiler() : impl_(std::make_unique<Impl>()) {}
Compiler::~Compiler() = default;

std::string_view vulkan_version_name(VulkanVersion version) noexcept {
    switch (version) {
    case VulkanVersion::v1_0: return "1.0";
    case VulkanVersion::v1_1: return "1.1";
    }
    return {};
}

std::string_view spirv_version_name(SpirvVersion version) noexcept {
    switch (version) {
    case SpirvVersion::v1_0: return "1.0";
    case SpirvVersion::v1_3: return "1.3";
    }
    return {};
}

std::string_view hlsl_shader_model_name(HlslShaderModel model) noexcept {
    switch (model) {
    case HlslShaderModel::v6_0: return "6.0";
    }
    return {};
}

std::span<const CompilerRoute> Compiler::routes() noexcept {
#if defined(TRUFFLE_SHADER_HAS_GLSLANG) || defined(TRUFFLE_SHADER_HAS_DXC)
    static constexpr rhi::ShaderStage stages[]{
        rhi::ShaderStage::vertex, rhi::ShaderStage::fragment, rhi::ShaderStage::compute};
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
    static constexpr VulkanTarget targets[]{
        {VulkanVersion::v1_0, SpirvVersion::v1_0},
        {VulkanVersion::v1_1, SpirvVersion::v1_0},
        {VulkanVersion::v1_1, SpirvVersion::v1_3}};
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
    static constexpr VulkanTarget dxcTargets[]{
        {VulkanVersion::v1_0, SpirvVersion::v1_0},
        {VulkanVersion::v1_1, SpirvVersion::v1_3}};
    static constexpr HlslShaderModel hlslModels[]{HlslShaderModel::v6_0};
#endif
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    static constexpr rhi::ShaderStage graphics[]{rhi::ShaderStage::vertex, rhi::ShaderStage::fragment};
    static constexpr MetalTarget metal[]{{}};
    static constexpr GlslTarget glsl[]{{}};
    static constexpr GlslEsTarget es[]{{}};
#endif
    static constexpr CompilerRoute enabled[]{
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
        {rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::spirv, stages, targets},
        {rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::spirv, stages, targets},
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        {rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::metal_source, stages, targets, metal, {}, {}, true},
        {rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::metal_source, stages, targets, metal, {}, {}, true},
        {rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::glsl, graphics, targets, {}, glsl},
        {rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::glsl, graphics, targets, {}, glsl},
        {rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::glsl_es, graphics, targets, {}, {}, es},
        {rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::glsl_es, graphics, targets, {}, {}, es},
#endif
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
        {.language = rhi::ShaderSourceLanguage::hlsl, .target = rhi::ShaderTarget::spirv,
         .stages = stages, .vulkanTargets = dxcTargets, .hlslShaderModels = hlslModels},
        {.language = rhi::ShaderSourceLanguage::hlsl, .target = rhi::ShaderTarget::dxil,
         .stages = stages, .hlslShaderModels = hlslModels},
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        {.language = rhi::ShaderSourceLanguage::hlsl, .target = rhi::ShaderTarget::metal_source,
         .stages = stages, .vulkanTargets = dxcTargets, .metalTargets = metal,
         .experimental = true, .hlslShaderModels = hlslModels},
        {.language = rhi::ShaderSourceLanguage::hlsl, .target = rhi::ShaderTarget::glsl,
         .stages = graphics, .vulkanTargets = dxcTargets, .glslTargets = glsl, .hlslShaderModels = hlslModels},
        {.language = rhi::ShaderSourceLanguage::hlsl, .target = rhi::ShaderTarget::glsl_es,
         .stages = graphics, .vulkanTargets = dxcTargets, .glslEsTargets = es, .hlslShaderModels = hlslModels},
#endif
#endif
    };
    return enabled;
#else
    return {};
#endif
}

bool Compiler::available() noexcept { return !routes().empty(); }

rhi::Result<Compilation> Compiler::compile(const CompileRequest& request) {
    if (vulkan_version_name(request.vulkanTarget.environment).empty() ||
        spirv_version_name(request.vulkanTarget.spirv).empty() ||
        hlsl_shader_model_name(request.hlslShaderModel).empty()) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Invalid Vulkan environment, SPIR-V version or HLSL shader model");
    }
    const auto enabled = routes();
    if (enabled.empty()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Shader compilers disabled: enable TRUFFLE_SHADER_ENABLE_GLSLANG or TRUFFLE_SHADER_ENABLE_DXC");
    }
    const auto route = std::find_if(enabled.begin(), enabled.end(), [&](const auto& value) {
        return value.language == request.language && value.target == request.target;
    });
    if (route == enabled.end()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
            "No enabled Shader compiler route: " +
            std::string{rhi::shader_source_language_name(request.language)} + " -> " +
            std::string{rhi::shader_target_name(request.target)});
    }
    if ((request.target == rhi::ShaderTarget::metal_source &&
         request.metalTarget.version != MetalVersion::v2_1) ||
        (request.target == rhi::ShaderTarget::glsl && request.glslTarget.version != GlslVersion::v410) ||
        (request.target == rhi::ShaderTarget::glsl_es && request.glslEsTarget.version != GlslEsVersion::v300)) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument, "Invalid native target version");
    }
    if (request.target == rhi::ShaderTarget::metal_source &&
        !request.allowExperimentalMetalGeneration) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Generated Metal requires explicit experimental opt-in");
    }
    if (std::find(route->stages.begin(), route->stages.end(), request.stage) == route->stages.end()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Shader stage not supported by the selected compiler route");
    }
    if (!route->vulkanTargets.empty() &&
        std::find(route->vulkanTargets.begin(), route->vulkanTargets.end(), request.vulkanTarget) ==
        route->vulkanTargets.end()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
            "Unsupported Vulkan/SPIR-V configuration: Vulkan " +
            std::string{vulkan_version_name(request.vulkanTarget.environment)} + " / SPIR-V " +
            std::string{spirv_version_name(request.vulkanTarget.spirv)});
    }
    if (request.target == rhi::ShaderTarget::dxil && request.vulkanTarget != VulkanTarget{})
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "DXIL does not accept Vulkan/SPIR-V settings");
#if defined(TRUFFLE_SHADER_HAS_DXC)
    if (request.language == rhi::ShaderSourceLanguage::hlsl)
        return compile_dxc(request, impl_->cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
    return compile_glslang(request, impl_->cache);
#else
    return rhi::Status::failure(rhi::StatusCode::unsupported,
                               "No enabled Shader compiler route");
#endif
}

void Compiler::clear_cache() { impl_->cache.clear(); }

rhi::Result<Compilation> RebuildState::rebuild(
    Compiler& compiler, const CompileRequest& request) {
    auto result = compiler.compile(request);
    if (result.ok()) {
        lastGood_ = std::make_unique<Compilation>(result.value());
    }
    return result;
}

const Compilation* RebuildState::last_good() const noexcept {
    return lastGood_.get();
}

rhi::Result<Source> read_source(std::string_view path) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(
        std::filesystem::path{path}, error);
    if (error) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Cannot resolve shader source: " + std::string{path});
    }
    std::ifstream stream{canonical, std::ios::binary};
    if (!stream) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Cannot read shader source: " + canonical.string());
    }
    std::string text{std::istreambuf_iterator<char>{stream}, {}};
    if (stream.bad()) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Shader source read failed: " + canonical.string());
    }
    return Source{canonical.generic_string(), std::move(text)};
}

} // namespace truffle::shader

#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#if defined(TRUFFLE_SHADER_HAS_METAL_COMPILER)
#include "metal_adapter.hpp"
#endif

namespace truffle::shader {

#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
rhi::Result<Compilation> compile_glslang(const CompileRequest& request,
                                       std::vector<Compilation>& cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_SLANG)
rhi::Result<Compilation> compile_slang(const CompileRequest& request,
                                     std::vector<Compilation>& cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_TINT)
rhi::Result<Compilation> compile_tint(const CompileRequest&, std::vector<Compilation>&);
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
    static const std::vector<CompilerRoute> enabled = [] {
        std::vector<CompilerRoute> result;
#if defined(TRUFFLE_SHADER_HAS_GLSLANG) || defined(TRUFFLE_SHADER_HAS_DXC) || \
    defined(TRUFFLE_SHADER_HAS_METAL_COMPILER) || \
    defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS) || defined(TRUFFLE_SHADER_HAS_TINT) || \
    defined(TRUFFLE_SHADER_HAS_SLANG)
        static constexpr rhi::ShaderStage stages[]{
            rhi::ShaderStage::vertex, rhi::ShaderStage::fragment,
            rhi::ShaderStage::compute};
#endif
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS) && \
    (defined(TRUFFLE_SHADER_HAS_GLSLANG) || defined(TRUFFLE_SHADER_HAS_DXC))
        static constexpr rhi::ShaderStage graphics[]{
            rhi::ShaderStage::vertex, rhi::ShaderStage::fragment};
#endif
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
        static constexpr VulkanTarget targets[]{
            {VulkanVersion::v1_0, SpirvVersion::v1_0},
            {VulkanVersion::v1_1, SpirvVersion::v1_0},
            {VulkanVersion::v1_1, SpirvVersion::v1_3}};
#endif
#if defined(TRUFFLE_SHADER_HAS_SLANG)
        static constexpr VulkanTarget slangTargets[]{
            {VulkanVersion::v1_1, SpirvVersion::v1_3}};
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
        static constexpr VulkanTarget dxcTargets[]{
            {VulkanVersion::v1_0, SpirvVersion::v1_0},
            {VulkanVersion::v1_1, SpirvVersion::v1_3}};
        static constexpr HlslShaderModel hlslModels[]{HlslShaderModel::v6_0};
#endif
#if defined(TRUFFLE_SHADER_HAS_TINT)
        static constexpr VulkanTarget tintTargets[]{
            {VulkanVersion::v1_1, SpirvVersion::v1_3}};
#endif
#if defined(TRUFFLE_SHADER_HAS_METAL_COMPILER) || \
    defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        static constexpr MetalTarget metal[]{{}};
#endif
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS) && \
    (defined(TRUFFLE_SHADER_HAS_GLSLANG) || defined(TRUFFLE_SHADER_HAS_DXC))
        static constexpr GlslTarget glsl[]{{}};
        static constexpr GlslEsTarget es[]{{}};
#endif
#if defined(TRUFFLE_SHADER_HAS_GLSLANG) || defined(TRUFFLE_SHADER_HAS_DXC) || \
    defined(TRUFFLE_SHADER_HAS_METAL_COMPILER) || \
    defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS) || defined(TRUFFLE_SHADER_HAS_TINT) || \
    defined(TRUFFLE_SHADER_HAS_SLANG)
        const auto add = [&](CompilerRoute route) {
            result.push_back(route);
        };
#endif
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
        add({rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::spirv,
             stages, targets});
        add({rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::spirv,
             stages, targets});
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        add({rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::metal_source,
             stages, targets, metal, {}, {}, true});
        add({rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::metal_source,
             stages, targets, metal, {}, {}, true});
        add({rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::glsl,
             graphics, targets, {}, glsl});
        add({rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::glsl,
             graphics, targets, {}, glsl});
        add({rhi::ShaderSourceLanguage::glsl, rhi::ShaderTarget::glsl_es,
             graphics, targets, {}, {}, es});
        add({rhi::ShaderSourceLanguage::glsl_es, rhi::ShaderTarget::glsl_es,
             graphics, targets, {}, {}, es});
#endif
#endif
#if defined(TRUFFLE_SHADER_HAS_SLANG)
        add({rhi::ShaderSourceLanguage::slang, rhi::ShaderTarget::spirv,
             stages, slangTargets});
#endif
#if defined(TRUFFLE_SHADER_HAS_TINT)
        add({rhi::ShaderSourceLanguage::wgsl, rhi::ShaderTarget::spirv,
             stages, tintTargets});
        add({rhi::ShaderSourceLanguage::wgsl, rhi::ShaderTarget::wgsl,
             stages, tintTargets, {}, {}, {}, true});
        add({rhi::ShaderSourceLanguage::spirv, rhi::ShaderTarget::wgsl,
             stages, tintTargets, {}, {}, {}, true});
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        add({rhi::ShaderSourceLanguage::wgsl, rhi::ShaderTarget::metal_source,
             stages, tintTargets, metal, {}, {}, true});
#endif
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
        add({.language = rhi::ShaderSourceLanguage::hlsl,
             .target = rhi::ShaderTarget::spirv,
             .stages = stages,
             .vulkanTargets = dxcTargets,
             .hlslShaderModels = hlslModels});
        add({.language = rhi::ShaderSourceLanguage::hlsl,
             .target = rhi::ShaderTarget::dxil,
             .stages = stages,
             .hlslShaderModels = hlslModels});
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        add({.language = rhi::ShaderSourceLanguage::hlsl,
             .target = rhi::ShaderTarget::metal_source,
             .stages = stages,
             .vulkanTargets = dxcTargets,
             .metalTargets = metal,
             .experimental = true,
             .hlslShaderModels = hlslModels});
        add({.language = rhi::ShaderSourceLanguage::hlsl,
             .target = rhi::ShaderTarget::glsl,
             .stages = graphics,
             .vulkanTargets = dxcTargets,
             .glslTargets = glsl,
             .hlslShaderModels = hlslModels});
        add({.language = rhi::ShaderSourceLanguage::hlsl,
             .target = rhi::ShaderTarget::glsl_es,
             .stages = graphics,
             .vulkanTargets = dxcTargets,
             .glslEsTargets = es,
             .hlslShaderModels = hlslModels});
#endif
#endif
#if defined(TRUFFLE_SHADER_HAS_METAL_COMPILER)
        static constexpr CompilerRoute nativeSource{
            rhi::ShaderSourceLanguage::msl, rhi::ShaderTarget::metal_source,
            stages, {}, metal, {}, {}, false};
        if (detail::native_msl_available()) add(nativeSource);
#if defined(TRUFFLE_SHADER_HAS_METALLIB_COMPILER)
        static constexpr CompilerRoute offlineLibrary{
            rhi::ShaderSourceLanguage::msl, rhi::ShaderTarget::metal_library,
            stages, {}, metal, {}, {}, false};
        if (detail::native_msl_available() &&
            detail::offline_metallib_available()) {
            add(offlineLibrary);
        }
#endif
#endif
        return result;
    }();
    return enabled;
}

bool Compiler::available() noexcept { return !routes().empty(); }

rhi::Result<Compilation> Compiler::compile(const CompileRequest& request) {
    const bool nativeMslRequest =
        request.language == rhi::ShaderSourceLanguage::msl &&
        (request.target == rhi::ShaderTarget::metal_source ||
         request.target == rhi::ShaderTarget::metal_library);
    if (!request.sourceUnits.empty() && !nativeMslRequest) {
        return rhi::Status::failure(
            rhi::StatusCode::unsupported,
            "CompileRequest::sourceUnits are supported only by native MSL routes");
    }
    if (request.interfaceDeclaration && !nativeMslRequest) {
        return rhi::Status::failure(
            rhi::StatusCode::unsupported,
            "Typed interface declarations are supported only by native MSL routes");
    }
    const bool nativeDxilRequest =
        request.language == rhi::ShaderSourceLanguage::hlsl &&
        request.target == rhi::ShaderTarget::dxil;
    if (!nativeMslRequest &&
        !nativeDxilRequest &&
        (vulkan_version_name(request.vulkanTarget.environment).empty() ||
         spirv_version_name(request.vulkanTarget.spirv).empty())) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Invalid Vulkan environment or SPIR-V version");
    }
    if (request.language == rhi::ShaderSourceLanguage::hlsl &&
        hlsl_shader_model_name(request.hlslShaderModel).empty()) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "Invalid HLSL shader model");
    }
    const auto enabled = routes();
    if (enabled.empty()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "No enabled Shader source-compilation routes are available in this build/runtime; enable an optional compiler adapter such as TRUFFLE_SHADER_ENABLE_GLSLANG, TRUFFLE_SHADER_ENABLE_DXC, TRUFFLE_SHADER_ENABLE_TINT or TRUFFLE_SHADER_ENABLE_SLANG");
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
    if (((request.target == rhi::ShaderTarget::metal_source ||
          request.target == rhi::ShaderTarget::metal_library) &&
         request.metalTarget.version != MetalVersion::v2_1) ||
        (request.target == rhi::ShaderTarget::glsl && request.glslTarget.version != GlslVersion::v410) ||
        (request.target == rhi::ShaderTarget::glsl_es && request.glslEsTarget.version != GlslEsVersion::v300)) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument, "Invalid native target version");
    }
    if ((request.target == rhi::ShaderTarget::metal_source ||
         request.target == rhi::ShaderTarget::metal_library) &&
        request.language != rhi::ShaderSourceLanguage::msl &&
        !request.allowExperimentalMetalGeneration) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Generated Metal requires explicit experimental opt-in");
    }
    if (request.target == rhi::ShaderTarget::wgsl &&
        !request.allowExperimentalWgslGeneration) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Generated WGSL requires explicit experimental opt-in");
    }
    if (std::find(route->stages.begin(), route->stages.end(), request.stage) == route->stages.end()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
                                   "Shader stage not supported by the selected compiler route");
    }
    if (!route->vulkanTargets.empty() &&
        std::find(route->vulkanTargets.begin(), route->vulkanTargets.end(),
                  request.vulkanTarget) == route->vulkanTargets.end()) {
        return rhi::Status::failure(rhi::StatusCode::unsupported,
            "Unsupported Vulkan/SPIR-V configuration: Vulkan " +
            std::string{vulkan_version_name(request.vulkanTarget.environment)} + " / SPIR-V " +
            std::string{spirv_version_name(request.vulkanTarget.spirv)});
    }
#if defined(TRUFFLE_SHADER_HAS_METAL_COMPILER)
    if (nativeMslRequest) {
        return detail::compile_msl(request, impl_->cache);
    }
#else
    if (nativeMslRequest) {
        return rhi::Status::failure(
            rhi::StatusCode::unsupported,
            "Native MSL compiler adapter is disabled");
    }
#endif
    if (request.target == rhi::ShaderTarget::dxil &&
        request.vulkanTarget != VulkanTarget{}) {
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "DXIL does not accept Vulkan/SPIR-V settings");
    }
#if defined(TRUFFLE_SHADER_HAS_TINT)
    if (request.language == rhi::ShaderSourceLanguage::wgsl ||
        request.language == rhi::ShaderSourceLanguage::spirv)
        return compile_tint(request, impl_->cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_DXC)
    if (request.language == rhi::ShaderSourceLanguage::hlsl) {
        return compile_dxc(request, impl_->cache);
    }
#endif
#if defined(TRUFFLE_SHADER_HAS_GLSLANG)
    if (request.language == rhi::ShaderSourceLanguage::glsl ||
        request.language == rhi::ShaderSourceLanguage::glsl_es)
        return compile_glslang(request, impl_->cache);
#endif
#if defined(TRUFFLE_SHADER_HAS_SLANG)
    if (request.language == rhi::ShaderSourceLanguage::slang)
        return compile_slang(request, impl_->cache);
#endif
    return rhi::Status::failure(rhi::StatusCode::unsupported,
                               "No enabled Shader compiler route");
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

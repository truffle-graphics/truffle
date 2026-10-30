#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace truffle;
    using shader::SpirvVersion;
    using shader::VulkanVersion;
    shader::Compiler compiler;
    shader::CompileRequest request;
    request.source = {"route.glsl", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    const auto routes = shader::Compiler::routes();
    check(shader::Compiler::available() == !routes.empty(), "Availability means any enabled source route");
    std::set<std::pair<rhi::ShaderSourceLanguage, rhi::ShaderTarget>> routeKeys;
    for (const auto& route : routes) {
        check(routeKeys.emplace(route.language, route.target).second,
              "Each implemented source/target route is advertised once");
    }

    auto invalidRequest = request;
    bool resolved = false;
    invalidRequest.source.text = "#version 450\n#include \"must-not-resolve.glsl\"\n";
    invalidRequest.resolver = [&](std::string_view, std::string_view, bool) -> rhi::Result<shader::Source> {
        resolved = true;
        return shader::Source{"unexpected", ""};
    };
    const auto expect = [&](const shader::CompileRequest& value, rhi::StatusCode code) {
        const auto result = compiler.compile(value);
        check(!result.ok() && result.status().code == code && !result.status().message.empty(),
              "Typed rejection with diagnostic");
        check(!resolved, "Rejected route/configuration must not invoke source resolution");
    };
    invalidRequest.vulkanTarget.environment = static_cast<VulkanVersion>(99);
    expect(invalidRequest, rhi::StatusCode::invalid_argument);
    invalidRequest.vulkanTarget = {};
    invalidRequest.vulkanTarget.spirv = static_cast<SpirvVersion>(99);
    expect(invalidRequest, rhi::StatusCode::invalid_argument);
    invalidRequest.vulkanTarget = {VulkanVersion::v1_0, SpirvVersion::v1_3};
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.vulkanTarget = {};
    invalidRequest.stage = static_cast<rhi::ShaderStage>(99);
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.stage = request.stage;
    invalidRequest.sourceUnits = {"shared.glsl"};
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.sourceUnits.clear();
    invalidRequest.interfaceDeclaration = rhi::ShaderReflection{};
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.interfaceDeclaration.reset();
#if defined(TRUFFLE_TEST_DXC)
    invalidRequest.language = rhi::ShaderSourceLanguage::hlsl;
    invalidRequest.target = rhi::ShaderTarget::spirv;
    invalidRequest.sourceUnits = {"shared.hlsl"};
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.language = request.language;
    invalidRequest.target = request.target;
    invalidRequest.sourceUnits.clear();
#endif
    invalidRequest.language = rhi::ShaderSourceLanguage::wgsl;
    invalidRequest.target = rhi::ShaderTarget::spirv;
    invalidRequest.sourceUnits = {"shared.wgsl"};
    expect(invalidRequest, rhi::StatusCode::unsupported);
    invalidRequest.language = request.language;
    invalidRequest.target = request.target;
    invalidRequest.sourceUnits.clear();

    constexpr rhi::ShaderSourceLanguage languages[]{
        rhi::ShaderSourceLanguage::slang, rhi::ShaderSourceLanguage::hlsl,
        rhi::ShaderSourceLanguage::glsl, rhi::ShaderSourceLanguage::glsl_es,
        rhi::ShaderSourceLanguage::wgsl, rhi::ShaderSourceLanguage::msl,
        rhi::ShaderSourceLanguage::spirv, rhi::ShaderSourceLanguage::dxil,
        rhi::ShaderSourceLanguage::metal_library, rhi::ShaderSourceLanguage::backend_native,
        static_cast<rhi::ShaderSourceLanguage>(255)};
    constexpr rhi::ShaderTarget targets[]{
        rhi::ShaderTarget::spirv, rhi::ShaderTarget::dxil, rhi::ShaderTarget::metal_library,
        rhi::ShaderTarget::metal_source, rhi::ShaderTarget::wgsl, rhi::ShaderTarget::glsl,
        rhi::ShaderTarget::glsl_es, static_cast<rhi::ShaderTarget>(255)};
    for (const auto language : languages) {
        if (language == rhi::ShaderSourceLanguage::msl) continue;
        auto sourceUnits = request;
        sourceUnits.language = language;
        sourceUnits.sourceUnits = {"must-not-resolve.source"};
        sourceUnits.resolver = invalidRequest.resolver;
        const auto result = compiler.compile(sourceUnits);
        check(!result.ok() && result.status().code == rhi::StatusCode::unsupported &&
                  result.status().message.find("sourceUnits") != std::string::npos,
              "Non-MSL sourceUnits are rejected before adapter discovery/dispatch");
        check(!resolved, "Rejected sourceUnits must not invoke source resolution");
    }
    if (std::none_of(routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::msl &&
                   route.target == rhi::ShaderTarget::metal_source;
        })) {
        auto nativeMsl = request;
        nativeMsl.language = rhi::ShaderSourceLanguage::msl;
        nativeMsl.target = rhi::ShaderTarget::metal_source;
        nativeMsl.sourceUnits = {"native.metal"};
        const auto result = compiler.compile(nativeMsl);
        check(!result.ok() && result.status().code == rhi::StatusCode::unsupported &&
                  result.status().message.find("sourceUnits") == std::string::npos,
              "MSL sourceUnits bypass the shared rejection and retain truthful missing-route diagnostics");
    }
    invalidRequest.stage = request.stage;
    for (const auto language : languages) {
        for (const auto target : targets) {
            const auto advertised = std::find_if(routes.begin(), routes.end(), [&](const auto& route) {
                return route.language == language && route.target == target;
            });
            if (advertised != routes.end()) continue;
            invalidRequest.language = language;
            invalidRequest.target = target;
            expect(invalidRequest, rhi::StatusCode::unsupported);
        }
    }
    if (routes.empty()) {
        expect(request, rhi::StatusCode::unsupported);
        compiler.clear_cache();
        expect(request, rhi::StatusCode::unsupported);
        return 0;
    }
    std::set<std::string> keys;
#if defined(TRUFFLE_TEST_METAL_COMPILER)
    const auto nativeMsl = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::msl &&
                   route.target == rhi::ShaderTarget::metal_source;
        });
    check(nativeMsl != routes.end() && nativeMsl->stages.size() == 3 &&
              nativeMsl->vulkanTargets.empty() &&
              nativeMsl->metalTargets.size() == 1 &&
              nativeMsl->experimental,
          "Native MSL advertises exact stages and Metal configuration");
#endif
#if defined(TRUFFLE_TEST_METALLIB_COMPILER)
    const auto nativeLibrary = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::msl &&
                   route.target == rhi::ShaderTarget::metal_library;
        });
    check(nativeLibrary != routes.end() &&
              nativeLibrary->stages.size() == 3 &&
              nativeLibrary->vulkanTargets.empty() &&
              nativeLibrary->metalTargets.size() == 1 &&
              nativeLibrary->experimental,
          "Offline metallib route advertises exact stages and Metal configuration");
#endif
#if defined(TRUFFLE_TEST_DXC)
    const auto hlslSpirv = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::hlsl &&
                   route.target == rhi::ShaderTarget::spirv;
        });
    const auto hlslDxil = std::find_if(
        routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::hlsl &&
                   route.target == rhi::ShaderTarget::dxil;
        });
    check(hlslSpirv != routes.end() && hlslSpirv->stages.size() == 3 &&
              hlslSpirv->vulkanTargets.size() == 2 &&
              hlslSpirv->hlslShaderModels.size() == 1 &&
              hlslDxil != routes.end() && hlslDxil->vulkanTargets.empty() &&
              hlslDxil->hlslShaderModels.size() == 1,
          "DXC routes preserve exact HLSL stages and shader models");
#endif
    if (std::none_of(routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::glsl &&
                   route.target == rhi::ShaderTarget::spirv;
        })) {
        return 0;
    }
    for (const auto& route : routes) {
        if (route.target != rhi::ShaderTarget::spirv ||
            (route.language != rhi::ShaderSourceLanguage::glsl &&
             route.language != rhi::ShaderSourceLanguage::glsl_es)) continue;
        check(route.target == rhi::ShaderTarget::spirv &&
              (route.language == rhi::ShaderSourceLanguage::glsl ||
               route.language == rhi::ShaderSourceLanguage::glsl_es),
              "No unimplemented adapters advertised");
        check(route.stages.size() == 3 && route.vulkanTargets.size() == 3,
              "Exact implemented stage/configuration support");
        for (const auto stage : route.stages) {
            for (const auto configuration : route.vulkanTargets) {
                request.stage = stage;
                request.language = route.language;
                request.target = route.target;
                request.vulkanTarget = configuration;
                const bool es = route.language == rhi::ShaderSourceLanguage::glsl_es;
                request.source.text = es ? "#version 310 es\nprecision highp float;\n" : "#version 450\n";
                switch (stage) {
                case rhi::ShaderStage::vertex:
                    request.source.text +=
                        "layout(set=0,binding=0) uniform Params { vec4 value; } params;\n"
                        "void main(){ gl_Position=params.value; }\n";
                    break;
                case rhi::ShaderStage::fragment:
                    request.source.text +=
                        "layout(set=0,binding=0) uniform Params { vec4 value; } params;\n"
                        "layout(location=0) out vec4 color;\nvoid main(){ color=params.value; }\n";
                    break;
                case rhi::ShaderStage::compute:
                    request.source.text +=
                        "layout(local_size_x=1) in;\n"
                        "layout(set=0,binding=0,std430) buffer Data { uint value; } data;\n"
                        "void main(){ data.value=gl_GlobalInvocationID.x; }\n";
                    break;
                }
                if (keys.empty()) return 0;
                auto result = compiler.compile(request);
                if (!result.ok()) std::cerr << result.status().message << '\n';
                check(result.ok() && !result.value().cacheHit, "Every advertised configuration compiles");
                check(keys.insert(result.value().cacheKey).second, "All effective route options distinguish cache identity");
                const auto& compilation = result.value();
                check(compilation.vulkanTarget == configuration, "Effective target returned");
                const auto& bindings = compilation.package.desc().variants.front().reflection.bindings;
                check(bindings.size() == 1 && bindings.front().group == 0 &&
                      bindings.front().binding == 0 &&
                      bindings.front().minimumSize == (stage == rhi::ShaderStage::compute ? 4u : 16u) &&
                      bindings.front().readOnly == (stage != rhi::ShaderStage::compute),
                      "Target versions preserve resource reflection and access");
                const auto& code = compilation.package.desc().variants.front().code;
                check(code.size() >= 20, "SPIR-V header present");
                std::uint32_t version = 0;
                std::memcpy(&version, code.data() + 4, sizeof(version));
                check(version == (configuration.spirv == SpirvVersion::v1_0 ? 0x00010000u : 0x00010300u),
                      "SPIR-V header matches requested version");
                const auto provenance = ";vulkan=" +
                    std::string{shader::vulkan_version_name(configuration.environment)} +
                    ";spirv=" + std::string{shader::spirv_version_name(configuration.spirv)};
                check(compilation.compiler.revision.find(provenance) != std::string::npos, "Compiler provenance includes effective target");
                auto loaded = rhi::ShaderPackage::load(compilation.package.bytes());
                check(loaded.ok() && loaded.value().desc().compilers.front() == compilation.compiler,
                      "Target provenance persists in serialized TSP");
                for (int repeat = 0; repeat < 2; ++repeat) {
                    auto hit = compiler.compile(request);
                    check(hit.ok() && hit.value().cacheHit && hit.value().cacheKey == compilation.cacheKey &&
                          hit.value().vulkanTarget == configuration &&
                          hit.value().package.bytes().size() == compilation.package.bytes().size() &&
                          std::equal(hit.value().package.bytes().begin(), hit.value().package.bytes().end(),
                                     compilation.package.bytes().begin()),
                          "Repeated cache hits preserve effective target and bytes");
                }
            }
        }
    }
    check(keys.size() == 18, "Two languages, three stages, three target configurations");
    request = {};
    request.source = {"default.vert", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    auto implicit = compiler.compile(request);
    request.vulkanTarget = {VulkanVersion::v1_1, SpirvVersion::v1_3};
    auto explicitDefault = compiler.compile(request);
    check(implicit.ok() && explicitDefault.ok() && explicitDefault.value().cacheHit &&
          implicit.value().cacheKey == explicitDefault.value().cacheKey,
          "Default remains exactly Vulkan 1.1/SPIR-V 1.3");
    request.language = rhi::ShaderSourceLanguage::glsl_es;
    check(!compiler.compile(request).ok(), "GLSL source cannot masquerade as GLSL ES");
    request.language = rhi::ShaderSourceLanguage::glsl;
    request.source.text = "#version 310 es\nvoid main(){ gl_Position=vec4(0); }\n";
    check(!compiler.compile(request).ok(), "GLSL ES source cannot masquerade as GLSL");
}

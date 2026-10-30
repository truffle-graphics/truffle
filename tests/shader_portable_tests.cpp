#include "shader_portable_fixtures.hpp"
#include "shader_test_utils.hpp"

#include <algorithm>
#include <iostream>
#include <set>

namespace {
using namespace truffle;

int run() {
    shader::Compiler compiler;
    std::set<std::string> keys;
    std::size_t count = 0;
    for (const auto& route : shader::Compiler::routes()) {
        if (route.target == rhi::ShaderTarget::spirv) continue;
        TRUFFLE_SHADER_CHECK(route.experimental == (route.target == rhi::ShaderTarget::metal_source));
        TRUFFLE_SHADER_CHECK((route.target == rhi::ShaderTarget::metal_source && route.metalTargets.size() == 1) ||
            (route.target == rhi::ShaderTarget::glsl && route.glslTargets.size() == 1) ||
            (route.target == rhi::ShaderTarget::glsl_es && route.glslEsTargets.size() == 1));
        for (const auto stage : route.stages) {
            for (const auto vulkan : route.vulkanTargets) {
                auto request = tests::portable_request(stage, route.target, route.language);
                request.vulkanTarget = vulkan;
                auto result = tests::require(compiler.compile(request));
                TRUFFLE_SHADER_CHECK(!result.cacheHit && keys.insert(result.cacheKey).second);
                TRUFFLE_SHADER_CHECK(result.compiler.revision.find("spirv-cross=") != std::string::npos);
                TRUFFLE_SHADER_CHECK(result.compiler.revision.find("spirv-tools=") != std::string::npos);
                auto loaded = tests::require(rhi::ShaderPackage::load(result.package.bytes(),
                    {.allowExperimentalMetalGeneration = true}));
                TRUFFLE_SHADER_CHECK(loaded.desc().remaps == result.package.desc().remaps);
                const auto& variant = loaded.desc().variants[0];
                TRUFFLE_SHADER_CHECK(variant.target == route.target &&
                    variant.format == rhi::ShaderByteFormat::native_source && variant.entryPoint == "main");
                if (route.target == rhi::ShaderTarget::metal_source) {
                    TRUFFLE_SHADER_CHECK(!rhi::ShaderPackage::load(result.package.bytes()).ok());
                    TRUFFLE_SHADER_CHECK(variant.nativeEntryPoint != "main");
                    request.allowExperimentalMetalGeneration = false;
                    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
                    request.allowExperimentalMetalGeneration = true;
                } else {
                    const std::string text{reinterpret_cast<const char*>(variant.code.data()), variant.code.size()};
                    TRUFFLE_SHADER_CHECK(text.starts_with(route.target == rhi::ShaderTarget::glsl
                        ? "#version 410" : "#version 300 es"));
                }
                if (stage == rhi::ShaderStage::fragment) {
                    TRUFFLE_SHADER_CHECK(variant.reflection.bindings.size() == 3 && loaded.desc().remaps.size() == 3);
                    if (route.target != rhi::ShaderTarget::metal_source) {
                        TRUFFLE_SHADER_CHECK(variant.reflection.bindings[0].name == variant.reflection.bindings[1].name);
                        TRUFFLE_SHADER_CHECK(loaded.desc().remaps[0].nativeBinding == loaded.desc().remaps[1].nativeBinding);
                    }
                }
                const auto hit = tests::require(compiler.compile(request));
                TRUFFLE_SHADER_CHECK(hit.cacheHit && hit.cacheKey == result.cacheKey);
                auto spirv = request;
                spirv.target = rhi::ShaderTarget::spirv;
                const auto baseline = tests::require(compiler.compile(spirv));
                auto merged = baseline.package.desc();
                merged.variants.push_back(variant);
                merged.remaps = loaded.desc().remaps;
                TRUFFLE_SHADER_CHECK(rhi::ShaderPackage::create(merged,
                    {.allowExperimentalMetalGeneration = true}).ok());
                if (stage == rhi::ShaderStage::fragment) {
                    merged.variants.back().reflection.bindings[0].binding = 99;
                    TRUFFLE_SHADER_CHECK(!rhi::ShaderPackage::create(merged,
                        {.allowExperimentalMetalGeneration = true}).ok());
                }
                ++count;
            }
        }
    }
    if (count == 0) return 0;
    TRUFFLE_SHADER_CHECK(count == 42);
    for (const auto target : {rhi::ShaderTarget::metal_source, rhi::ShaderTarget::glsl, rhi::ShaderTarget::glsl_es}) {
        auto array = tests::portable_request(rhi::ShaderStage::vertex, target);
        array.source.text = "#version 450\nlayout(set=0,binding=0) uniform U { vec4 value; } u[2];\n"
                            "void main(){gl_Position=u[0].value+u[1].value;}\n";
        const auto result = compiler.compile(array);
        TRUFFLE_SHADER_CHECK(!result.ok() &&
            result.status().message.find("scalar resource descriptors") != std::string::npos);
        auto image = tests::portable_request(rhi::ShaderStage::fragment, target);
        image.source.text = "#version 450\nlayout(set=0,binding=0,rgba32f) readonly uniform image2D image;\n"
                            "layout(location=0) out vec4 color;\nvoid main(){color=imageLoad(image,ivec2(0));}\n";
        const auto storage = compiler.compile(image);
        TRUFFLE_SHADER_CHECK(!storage.ok() && storage.status().code == rhi::StatusCode::unsupported);
    }
    for (const auto target : {rhi::ShaderTarget::glsl, rhi::ShaderTarget::glsl_es}) {
        auto request = tests::portable_request(rhi::ShaderStage::compute, target);
        const auto result = compiler.compile(request);
        TRUFFLE_SHADER_CHECK(!result.ok() && result.status().code == rhi::StatusCode::unsupported);
        request = tests::portable_request(rhi::ShaderStage::fragment, target);
        request.source.text += "\nlayout(constant_id=9) const float unsupported=1.0;\n";
        TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
        request = tests::portable_request(rhi::ShaderStage::fragment, target);
        request.source.text.insert(request.source.text.find("void main"),
            "layout(set=1,binding=8) uniform sampler secondState;\n");
        request.source.text.replace(request.source.text.find("*tint.value"),
            std::string{"*tint.value"}.size(), "+texture(sampler2D(image,secondState),incomingUV)*tint.value");
        const auto graph = compiler.compile(request);
        TRUFFLE_SHADER_CHECK(!graph.ok() && graph.status().message.find("one-to-one") != std::string::npos);
    }
    auto request = tests::portable_request(rhi::ShaderStage::vertex, rhi::ShaderTarget::glsl);
    request.glslTarget.version = static_cast<shader::GlslVersion>(99);
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.glslTarget = {};
    request.target = rhi::ShaderTarget::metal_source;
    request.metalTarget.version = static_cast<shader::MetalVersion>(99);
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.metalTarget = {};
    request.target = rhi::ShaderTarget::glsl_es;
    request.glslEsTarget.version = static_cast<shader::GlslEsVersion>(99);
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.glslEsTarget = {};
    request.target = rhi::ShaderTarget::glsl;
    request.source.text += "\n#include \"value.glsl\"\n";
    std::string include = "// one\n";
    request.resolver = [&](std::string_view, std::string_view, bool) -> rhi::Result<shader::Source> {
        return shader::Source{"value.glsl", include};
    };
    shader::RebuildState rebuild;
    const auto first = tests::require(rebuild.rebuild(compiler, request));
    include = "// two\n";
    const auto second = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(first.cacheKey != second.cacheKey);
    request.defines.push_back({"OPTION", "1"});
    const auto third = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(third.cacheKey != second.cacheKey);
    request.generateDebugInfo = true;
    const auto debug = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(debug.cacheKey != third.cacheKey);
    request.source.text += "\n// root edit\n";
    const auto edited = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(edited.cacheKey != debug.cacheKey);
    request.source.text = "invalid source";
    TRUFFLE_SHADER_CHECK(!rebuild.rebuild(compiler, request).ok() &&
                         rebuild.last_good()->cacheKey == edited.cacheKey);
    std::cout << "42 portable native configurations: validated, serialized, remapped and cached\n";
    return 0;
}
}

int main() {
    try { return run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

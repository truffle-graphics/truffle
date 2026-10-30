#include "shader_hlsl_fixtures.hpp"
#include "shader_test_utils.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#if defined(TRUFFLE_TEST_DXC)
#include "dxil_reflection.hpp"
#endif

namespace {
using namespace truffle;

void stages() {
    shader::Compiler compiler;
    const auto routes = shader::Compiler::routes();
    std::set<std::string> keys;
    std::size_t count = 0;
    for (const auto& route : routes) {
        if (route.language != rhi::ShaderSourceLanguage::hlsl) continue;
        if (route.target != rhi::ShaderTarget::spirv && route.target != rhi::ShaderTarget::dxil) continue;
        TRUFFLE_SHADER_CHECK(route.stages.size() == 3);
        TRUFFLE_SHADER_CHECK(route.hlslShaderModels.size() == 1);
        TRUFFLE_SHADER_CHECK(route.hlslShaderModels[0] == shader::HlslShaderModel::v6_0);
        TRUFFLE_SHADER_CHECK(route.target == rhi::ShaderTarget::spirv ||
                             route.target == rhi::ShaderTarget::dxil);
        TRUFFLE_SHADER_CHECK(route.vulkanTargets.size() ==
                             (route.target == rhi::ShaderTarget::spirv ? 2 : 0));
        for (const auto stage : route.stages) {
            const auto compile = [&](shader::VulkanTarget target) {
                auto request = tests::hlsl_request(stage, route.target);
                request.vulkanTarget = target;
                auto result = tests::require(compiler.compile(request));
                ++count;
                TRUFFLE_SHADER_CHECK(keys.insert(result.cacheKey).second);
                TRUFFLE_SHADER_CHECK(result.compiler.name == "dxc");
                TRUFFLE_SHADER_CHECK(result.compiler.revision.starts_with(
                    "01b62ad47db7dee3dd33f90e7339cf9e860b3f93;truffle-dxc=1;"));
                TRUFFLE_SHADER_CHECK(result.dependencies.size() == 3 && !result.cacheHit);
                TRUFFLE_SHADER_CHECK(tests::require(compiler.compile(request)).cacheHit);
                const auto loaded = tests::require(rhi::ShaderPackage::load(result.package.bytes()));
                const auto& variant = loaded.desc().variants.front();
                TRUFFLE_SHADER_CHECK(variant.entryPoint == request.entryPoint && variant.stage == stage);
                const auto& reflection = variant.reflection;
                if (stage == rhi::ShaderStage::vertex) {
                    TRUFFLE_SHADER_CHECK(reflection.bindings.empty());
                    TRUFFLE_SHADER_CHECK(reflection.interfaces.size() == 2);
                } else {
                    TRUFFLE_SHADER_CHECK(reflection.bindings.size() ==
                                         (stage == rhi::ShaderStage::fragment ? 1 : 2));
                    const auto uniform = std::find_if(reflection.bindings.begin(), reflection.bindings.end(),
                        [](const auto& binding) { return binding.binding == 7; });
                    TRUFFLE_SHADER_CHECK(uniform != reflection.bindings.end() &&
                        uniform->group == 1 && uniform->readOnly && uniform->minimumSize == 16);
                }
                if (stage == rhi::ShaderStage::compute) {
                    TRUFFLE_SHADER_CHECK(reflection.requiredWorkgroupSize == (rhi::Extent3D{2, 1, 1}));
                    TRUFFLE_SHADER_CHECK(reflection.interfaces.empty());
                    const auto storage = std::find_if(reflection.bindings.begin(), reflection.bindings.end(),
                        [](const auto& binding) { return binding.binding == 9; });
                    TRUFFLE_SHADER_CHECK(storage != reflection.bindings.end() &&
                        storage->group == 1 && !storage->readOnly && storage->minimumSize == 4);
                } else {
                    TRUFFLE_SHADER_CHECK(reflection.interfaces.size() == 2);
                    const auto varying = std::find_if(reflection.interfaces.begin(), reflection.interfaces.end(),
                        [&](const auto& value) {
                            return value.direction == (stage == rhi::ShaderStage::vertex
                                ? rhi::ShaderInterfaceDirection::output : rhi::ShaderInterfaceDirection::input);
                        });
                    TRUFFLE_SHADER_CHECK(varying != reflection.interfaces.end() &&
                        varying->location == 0 && varying->type == "vec4" && varying->interpolation == "flat");
                }
                if (route.target == rhi::ShaderTarget::spirv) {
                    std::uint32_t version = 0;
                    std::memcpy(&version, variant.code.data() + 4, sizeof(version));
                    TRUFFLE_SHADER_CHECK(version == (target.spirv == shader::SpirvVersion::v1_0
                        ? 0x00010000u : 0x00010300u));
                } else {
                    TRUFFLE_SHADER_CHECK(variant.code.size() > 4 &&
                        std::memcmp(variant.code.data(), "DXBC", 4) == 0);
                }
            };
            if (route.vulkanTargets.empty()) compile({});
            else for (const auto target : route.vulkanTargets) compile(target);
        }
    }
    TRUFFLE_SHADER_CHECK(count == 9);
}

void portable() {
    shader::Compiler compiler;
    std::size_t count = 0;
    for (const auto& route : shader::Compiler::routes()) {
        if (route.language != rhi::ShaderSourceLanguage::hlsl ||
            route.target == rhi::ShaderTarget::spirv || route.target == rhi::ShaderTarget::dxil) continue;
        for (const auto stage : route.stages) {
            for (const auto target : route.vulkanTargets) {
                auto request = tests::hlsl_request(stage, route.target);
                request.vulkanTarget = target;
                request.allowExperimentalMetalGeneration = true;
                const auto result = tests::require(compiler.compile(request));
                const auto package = tests::require(rhi::ShaderPackage::load(
                    result.package.bytes(), {.allowExperimentalMetalGeneration = true}));
                const auto& variant = package.desc().variants.front();
                TRUFFLE_SHADER_CHECK(variant.format == rhi::ShaderByteFormat::native_source);
                TRUFFLE_SHADER_CHECK(!variant.code.empty());
                TRUFFLE_SHADER_CHECK(result.compiler.revision.find(";spirv-cross=") != std::string::npos);
                TRUFFLE_SHADER_CHECK(tests::require(compiler.compile(request)).cacheHit);
                ++count;
            }
        }
    }
    TRUFFLE_SHADER_CHECK(count == 0 || count == 14);
}

void rebuilds(rhi::ShaderTarget target) {
    shader::Compiler compiler;
    shader::RebuildState rebuild;
    auto request = tests::hlsl_request(rhi::ShaderStage::compute, target);
    request.source.text += "\n#include \"common.hlsli\"\n";
    auto common = tests::require(shader::read_source(
        std::string{TRUFFLE_HLSL_FIXTURE_DIR} + "/common.hlsli")).text;
    auto configuration = tests::require(shader::read_source(
        std::string{TRUFFLE_HLSL_FIXTURE_DIR} + "/configuration.hlsli")).text;
    std::size_t resolutions = 0;
    request.resolver = [&](std::string_view candidate, std::string_view including, bool system)
        -> rhi::Result<shader::Source> {
        ++resolutions;
        TRUFFLE_SHADER_CHECK(including.empty() && !system);
        if (std::filesystem::path{candidate}.filename() == "common.hlsli")
            return shader::Source{"virtual/common.hlsli", common};
        if (std::filesystem::path{candidate}.filename() == "configuration.hlsli")
            return shader::Source{"virtual/configuration.hlsli", configuration};
        return rhi::Status::failure(rhi::StatusCode::invalid_argument, "Missing " + std::string{candidate});
    };
    const auto first = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(resolutions != 0 && !first.cacheHit);
    TRUFFLE_SHADER_CHECK(first.dependencies.size() == 3);
    const auto before = resolutions;
    TRUFFLE_SHADER_CHECK(tests::require(rebuild.rebuild(compiler, request)).cacheHit);
    TRUFFLE_SHADER_CHECK(resolutions > before);
    std::reverse(request.defines.begin(), request.defines.end());
    TRUFFLE_SHADER_CHECK(tests::require(rebuild.rebuild(compiler, request)).cacheKey == first.cacheKey);
    configuration += "\n// changed dependency\n";
    const auto changed = tests::require(rebuild.rebuild(compiler, request));
    TRUFFLE_SHADER_CHECK(!changed.cacheHit && changed.cacheKey != first.cacheKey);
    configuration += "\n#error dependency_failure\n";
    const auto failure = rebuild.rebuild(compiler, request);
    TRUFFLE_SHADER_CHECK(!failure.ok() && failure.status().message.find("dependency_failure") != std::string::npos);
    TRUFFLE_SHADER_CHECK(rebuild.last_good() && rebuild.last_good()->cacheKey == changed.cacheKey);
    configuration.resize(configuration.find("\n#error"));
    TRUFFLE_SHADER_CHECK(tests::require(rebuild.rebuild(compiler, request)).cacheKey == changed.cacheKey);
    request.sourceUnits = {"extra.hlsli"};
    const auto sourceUnits = compiler.compile(request);
    TRUFFLE_SHADER_CHECK(!sourceUnits.ok() &&
        sourceUnits.status().code == rhi::StatusCode::unsupported &&
        sourceUnits.status().message.find("sourceUnits") != std::string::npos);
    request.sourceUnits.clear();
    request.defines.push_back({"QUALITY", "2"});
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.defines.pop_back();
    request.entryPoint = "missing";
    TRUFFLE_SHADER_CHECK(!compiler.compile(request).ok());
    request.entryPoint = "computeMain";
    request.hlslShaderModel = static_cast<shader::HlslShaderModel>(255);
    const auto invalid = compiler.compile(request);
    TRUFFLE_SHADER_CHECK(!invalid.ok() && invalid.status().code == rhi::StatusCode::invalid_argument);
    request.hlslShaderModel = shader::HlslShaderModel::v6_0;
    if (target == rhi::ShaderTarget::spirv) {
        request.vulkanTarget = {shader::VulkanVersion::v1_1, shader::SpirvVersion::v1_0};
        TRUFFLE_SHADER_CHECK(compiler.compile(request).status().code == rhi::StatusCode::unsupported);
    }
}

void resources(rhi::ShaderTarget target) {
    shader::Compiler compiler;
    auto request = tests::hlsl_request(rhi::ShaderStage::fragment, target);
    request.entryPoint = "sampleMain";
    request.source.text = R"(
cbuffer Params : register(b7, space1) { float4 tint; };
Texture2D<float4> images[2] : register(t3, space1);
SamplerState sampleState : register(s6, space1);
float4 sampleMain(float2 uv : TEXCOORD0) : SV_Target0 {
    return images[0].Sample(sampleState, uv) * images[1].Sample(sampleState, uv) * tint;
}
)";
    auto compiled = tests::require(compiler.compile(request));
    const auto& bindings = compiled.package.desc().variants.front().reflection.bindings;
    TRUFFLE_SHADER_CHECK(bindings.size() == 3);
    const auto images = std::find_if(bindings.begin(), bindings.end(), [](const auto& value) {
        return value.binding == 3;
    });
    TRUFFLE_SHADER_CHECK(images != bindings.end() && images->arrayCount == 2 &&
        images->type == rhi::ResourceBindingType::texture && images->readOnly);
    request.source.text = R"(
#ifdef __spirv__
cbuffer TargetParams : register(b2) { float4 value; };
#else
cbuffer TargetParams : register(b3) { float4 value; };
#endif
float4 sampleMain() : SV_Target0 { return value; }
)";
    compiled = tests::require(compiler.compile(request));
    const auto& reflected = compiled.package.desc().variants.front().reflection.bindings;
    TRUFFLE_SHADER_CHECK(reflected.size() == 1 &&
        reflected[0].binding == (target == rhi::ShaderTarget::spirv ? 2 : 3));
    if (target == rhi::ShaderTarget::dxil) {
        request.stage = rhi::ShaderStage::vertex;
        request.source.text = "float4 sampleMain(float4 v:POSITION):SV_Position { return v; }";
        const auto unrepresentable = compiler.compile(request);
        TRUFFLE_SHADER_CHECK(!unrepresentable.ok() &&
            unrepresentable.status().code == rhi::StatusCode::unsupported &&
            unrepresentable.status().message.find("TEXCOORD") != std::string::npos);
    }
}

#if defined(TRUFFLE_TEST_DXC)
void validator() {
    const auto utils = tests::require(shader::detail::dxc::instance<IDxcUtils>(CLSID_DxcUtils));
    auto request = tests::hlsl_request(rhi::ShaderStage::compute, rhi::ShaderTarget::dxil);
    auto result = tests::require(shader::Compiler{}.compile(request));
    auto code = result.package.desc().variants.front().code;
    code.front() = std::byte{0};
    IDxcBlobEncoding* raw = nullptr;
    TRUFFLE_SHADER_CHECK(SUCCEEDED(utils->CreateBlob(code.data(), static_cast<UINT32>(code.size()), 0, &raw)));
    const shader::detail::dxc::ComPtr<IDxcBlobEncoding> blob{raw};
    TRUFFLE_SHADER_CHECK(blob != nullptr);
    const auto status = shader::detail::validate_dxil(*blob);
    TRUFFLE_SHADER_CHECK(!status.ok() && status.message.find("DXIL validation failed") != std::string::npos);
}
#endif

int equivalence(int argc, char** argv) {
    if (argc != 5) throw std::runtime_error("Expected target, stage, Vulkan version and CLI package");
    const auto target = tests::require(rhi::shader_target_from_name(argv[1]));
    const std::string stageName = argv[2];
    const auto stage = stageName == "vertex" ? rhi::ShaderStage::vertex :
                       stageName == "fragment" ? rhi::ShaderStage::fragment : rhi::ShaderStage::compute;
    auto request = tests::hlsl_request(stage, target);
    request.allowExperimentalMetalGeneration = target == rhi::ShaderTarget::metal_source;
    if (std::string_view{argv[3]} == "1.0")
        request.vulkanTarget = {shader::VulkanVersion::v1_0, shader::SpirvVersion::v1_0};
    const auto result = tests::require(shader::Compiler{}.compile(request));
    std::ifstream input{argv[4], std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    TRUFFLE_SHADER_CHECK(input && bytes.size() == result.package.bytes().size());
    TRUFFLE_SHADER_CHECK(std::memcmp(bytes.data(), result.package.bytes().data(), bytes.size()) == 0);
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1) return equivalence(argc, argv);
        if (!truffle::tests::shader_route_enabled(truffle::rhi::ShaderSourceLanguage::hlsl)) {
            auto request = truffle::tests::hlsl_request(truffle::rhi::ShaderStage::compute,
                                                       truffle::rhi::ShaderTarget::spirv);
            TRUFFLE_SHADER_CHECK(truffle::shader::Compiler{}.compile(request).status().code ==
                                 truffle::rhi::StatusCode::unsupported);
            return 0;
        }
        stages();
        portable();
        rebuilds(truffle::rhi::ShaderTarget::spirv);
        rebuilds(truffle::rhi::ShaderTarget::dxil);
        resources(truffle::rhi::ShaderTarget::spirv);
        resources(truffle::rhi::ShaderTarget::dxil);
#if defined(TRUFFLE_TEST_DXC)
        validator();
#endif
    } catch (const std::exception& error) {
        std::cerr << "HLSL: " << error.what() << '\n';
        return 1;
    }
}

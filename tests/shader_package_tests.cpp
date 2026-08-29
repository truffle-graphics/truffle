#include "truffle/rhi/shader_package.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

truffle::rhi::ShaderReflection reflection() {
    using namespace truffle::rhi;
    return {
        .bindings = {
            {.name = "albedo",
             .stage = ShaderStage::fragment,
             .type = ResourceBindingType::texture,
             .group = 1,
             .binding = 3,
             .arrayCount = 4},
            {.name = "frame",
             .stage = ShaderStage::fragment,
             .type = ResourceBindingType::buffer,
             .group = 0,
             .binding = 0,
             .minimumSize = 64},
        },
        .pushConstants = {{.stage = ShaderStage::fragment,
                           .offset = 0,
                           .size = 16}},
        .specializationConstants = {{.id = 7,
                                     .name = "sample_count",
                                     .type = ShaderValueType::uint32,
                                     .defaultValueBits = 1}},
        .interfaces = {{.stage = ShaderStage::fragment,
                        .location = 0,
                        .name = "color",
                        .type = "float4",
                        .interpolation = "perspective"}},
    };
}

truffle::rhi::ShaderPackageDesc package_desc() {
    using namespace truffle::rhi;
    const auto reflected = reflection();
    return {
        .name = "deterministic-material",
        .requiredFeatures = {Feature::transfer, Feature::memory_budget,
                             Feature::transfer},
        .permutations = {{
            .name = "lit",
            .defines = {{.name = "USE_SHADOWS", .value = "1"},
                        {.name = "ALPHA_TEST", .value = "0"}},
            .specializationConstants = {{.id = 7,
                                         .name = "sample_count",
                                         .type = ShaderValueType::uint32,
                                         .defaultValueBits = 1}},
        }},
        .remaps = {
            {.target = ShaderTarget::dxil,
             .stage = ShaderStage::fragment,
             .group = 1,
             .binding = 3,
             .arrayElement = 0,
             .nativeGroup = 0,
             .nativeBinding = 5,
             .nativeArrayElement = 0},
            {.target = ShaderTarget::spirv,
             .stage = ShaderStage::fragment,
             .group = 1,
             .binding = 3,
             .arrayElement = 0,
             .nativeGroup = 1,
             .nativeBinding = 3,
             .nativeArrayElement = 0},
        },
        .sources = {{.path = "material.slang",
                     .language = ShaderSourceLanguage::slang,
                     .sha256 = std::string(64, 'A')}},
        .compilers = {{.name = "fixture-compiler",
                       .version = "1.2.3",
                       .revision = "abc123"}},
        .diagnostics = {{.severity = ShaderDiagnosticSeverity::info,
                         .variant = "lit",
                         .message = "fixture compiled"}},
        .variants = {
            {.target = ShaderTarget::dxil,
             .format = ShaderByteFormat::dxil,
             .kind = ShaderVariantKind::precompiled,
             .stage = ShaderStage::fragment,
             .entryPoint = "shade",
             .permutation = "lit",
             .reflection = reflected,
             .code = {std::byte{0x44}, std::byte{0x58}}},
            {.target = ShaderTarget::spirv,
             .format = ShaderByteFormat::spirv,
             .kind = ShaderVariantKind::generated,
             .stage = ShaderStage::fragment,
             .entryPoint = "shade",
             .permutation = "lit",
             .reflection = reflected,
             .code = {std::byte{0x03}, std::byte{0x02}}},
            {.target = ShaderTarget::spirv,
             .format = ShaderByteFormat::spirv,
             .kind = ShaderVariantKind::native_override,
             .stage = ShaderStage::fragment,
             .entryPoint = "shade",
             .permutation = "lit",
             .reflection = reflected,
             .code = {std::byte{0x13}, std::byte{0x37}}},
        },
    };
}

truffle::rhi::ShaderPackageDesc single_variant(
    truffle::rhi::ShaderTarget target,
    truffle::rhi::ShaderVariantKind kind) {
    using namespace truffle::rhi;
    auto format = ShaderByteFormat::native_source;
    if (target == ShaderTarget::metal_library) {
        format = ShaderByteFormat::metal_library;
    }
    return {
        .name = "gate-fixture",
        .sources = {{.path = "fixture.shader",
                     .language = ShaderSourceLanguage::backend_native,
                     .sha256 = std::string(64, '1')}},
        .compilers = {{.name = "fixture", .version = "1"}},
        .variants = {{.target = target,
                      .format = format,
                      .kind = kind,
                      .stage = ShaderStage::compute,
                      .code = {std::byte{1}}}},
    };
}

} // namespace

int main() {
    using namespace truffle::rhi;

    auto firstDesc = package_desc();
    auto secondDesc = firstDesc;
    std::reverse(secondDesc.requiredFeatures.begin(),
                 secondDesc.requiredFeatures.end());
    std::reverse(secondDesc.remaps.begin(), secondDesc.remaps.end());
    std::reverse(secondDesc.variants.begin(), secondDesc.variants.end());
    std::reverse(secondDesc.permutations[0].defines.begin(),
                 secondDesc.permutations[0].defines.end());

    auto firstResult = ShaderPackage::create(std::move(firstDesc));
    auto secondResult = ShaderPackage::create(std::move(secondDesc));
    assert(firstResult.ok() && secondResult.ok());
    auto first = std::move(firstResult).value();
    auto second = std::move(secondResult).value();
    assert(std::equal(first.bytes().begin(), first.bytes().end(),
                      second.bytes().begin(), second.bytes().end()));
    assert(first.desc().sources[0].sha256 == std::string(64, 'a'));

    const auto* selected = first.find_variant(
        ShaderTarget::spirv, "shade", ShaderStage::fragment, "lit");
    assert(selected != nullptr);
    assert(selected->kind == ShaderVariantKind::native_override);
    assert(selected->code[0] == std::byte{0x13});

    auto loadedResult = ShaderPackage::load(first.bytes());
    assert(loadedResult.ok());
    auto loaded = std::move(loadedResult).value();
    assert(loaded.desc().name == "deterministic-material");
    assert(loaded.find_variant(ShaderTarget::dxil, "shade",
                               ShaderStage::fragment, "lit") != nullptr);
    auto instanceResult = create_null_instance();
    assert(instanceResult.ok());
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    auto shaderResult = device.create_shader(
        loaded, ShaderTarget::spirv, "shade", ShaderStage::fragment, "lit");
    assert(shaderResult.ok());
    assert(shaderResult.value().reflection().find(
               1, 3, ShaderStage::fragment) != nullptr);
    auto unavailableShader = device.create_shader(
        loaded, ShaderTarget::wgsl, "shade", ShaderStage::fragment, "lit");
    assert(!unavailableShader.ok());
    assert(unavailableShader.status().code == StatusCode::unsupported);
    assert(unavailableShader.status().message.find("wgsl") !=
           std::string::npos);

    ShaderPackageOptions missingCapabilities{
        .availableFeatures = {Feature::transfer},
        .enforceCapabilities = true,
    };
    auto capabilityMismatch =
        ShaderPackage::load(first.bytes(), missingCapabilities);
    assert(!capabilityMismatch.ok());
    assert(capabilityMismatch.status().code == StatusCode::unsupported);
    ShaderPackageOptions matchingCapabilities{
        .availableFeatures = {Feature::memory_budget, Feature::transfer},
        .enforceCapabilities = true,
    };
    assert(ShaderPackage::load(first.bytes(), matchingCapabilities).ok());

    auto mismatchDesc = package_desc();
    mismatchDesc.variants[0].reflection.bindings[0].arrayCount = 2;
    auto mismatch = ShaderPackage::create(std::move(mismatchDesc));
    assert(!mismatch.ok());
    assert(mismatch.status().code == StatusCode::invalid_argument);

    std::vector<std::byte> corrupt{first.bytes().begin(), first.bytes().end()};
    corrupt.back() ^= std::byte{1};
    assert(!ShaderPackage::load(corrupt).ok());
    std::vector<std::byte> trailing{first.bytes().begin(), first.bytes().end()};
    trailing.push_back(std::byte{0});
    assert(!ShaderPackage::load(trailing).ok());

    auto generatedWgsl = ShaderPackage::create(
        single_variant(ShaderTarget::wgsl, ShaderVariantKind::generated));
    assert(!generatedWgsl.ok());
    assert(generatedWgsl.status().code == StatusCode::unsupported);
    ShaderPackageOptions allowWgsl{.allowExperimentalWgslGeneration = true};
    assert(ShaderPackage::create(
               single_variant(ShaderTarget::wgsl,
                              ShaderVariantKind::generated),
               allowWgsl)
               .ok());
    assert(ShaderPackage::create(
               single_variant(ShaderTarget::wgsl,
                              ShaderVariantKind::native_override))
               .ok());

    auto generatedMetal = ShaderPackage::create(single_variant(
        ShaderTarget::metal_source, ShaderVariantKind::generated));
    assert(!generatedMetal.ok());
    assert(generatedMetal.status().code == StatusCode::unsupported);
    ShaderPackageOptions allowMetal{.allowExperimentalMetalGeneration = true};
    assert(ShaderPackage::create(
               single_variant(ShaderTarget::metal_source,
                              ShaderVariantKind::generated),
               allowMetal)
               .ok());

    auto badHash = package_desc();
    badHash.sources[0].sha256 = "not-a-sha256";
    assert(!ShaderPackage::create(std::move(badHash)).ok());
    assert(shader_target_from_name("spirv").ok());
    assert(!shader_target_from_name("unknown").ok());
    assert(shader_source_language_from_name("slang").ok());
    assert(!shader_source_language_from_name("unknown").ok());
    return 0;
}

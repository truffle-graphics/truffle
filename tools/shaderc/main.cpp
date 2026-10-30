#include "truffle/rhi/shader_package.hpp"

#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using truffle::rhi::Result;
using truffle::rhi::ShaderByteFormat;
using truffle::rhi::ShaderPackage;
using truffle::rhi::ShaderPackageDesc;
using truffle::rhi::ShaderStage;
using truffle::rhi::ShaderTarget;
using truffle::rhi::ShaderVariantDesc;
using truffle::rhi::ShaderVariantKind;
using truffle::rhi::Status;
using truffle::rhi::StatusCode;

struct Arguments {
    std::string name;
    std::string target;
    std::string stage;
    std::string entry = "main";
    std::string input;
    std::string output;
    std::string kind = "precompiled";
    std::string sourceHash;
    std::string sourceLanguage;
    std::string compilerName;
    std::string compilerVersion;
    std::string compilerRevision;
    std::string append;
    std::vector<std::string> includeRoots;
    std::vector<truffle::rhi::ShaderDefine> defines;
    truffle::shader::VulkanTarget vulkanTarget;
    truffle::shader::HlslShaderModel hlslShaderModel = truffle::shader::HlslShaderModel::v6_0;
    bool hlslModelSpecified = false;
    bool targetSettings = false;
    bool compile = false;
    bool experimentalMetal = false;
    std::string nativeVersionTarget;
};

[[nodiscard]] Status invalid(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

void usage(std::ostream& output) {
    output
        << "Usage: truffle-shaderc --name NAME --target TARGET --stage STAGE "
           "--input FILE --output FILE.tsp "
           "--source-language LANGUAGE "
           "[--source-hash SHA256] [--compiler-name NAME --compiler-version VERSION] "
           "[--compiler-revision REV] [--entry NAME] "
           "[--kind precompiled|native|generated] [--append PACKAGE] "
           "[--compile [--include-root DIR]... [--define NAME[=VALUE]]... "
           "[--vulkan-version VERSION] [--spirv-version VERSION] "
           "[--hlsl-shader-model 6.0] "
           "[--msl-version 2.1|--glsl-version 410|--glsl-es-version 300] "
           "[--allow-experimental-metal]]\n"
        << "       truffle-shaderc --routes\n"
        << "       truffle-shaderc --inspect PACKAGE [--allow-experimental-metal]\n"
        << "Targets: spirv, dxil, metallib, msl, wgsl, glsl, glsl-es\n"
        << "Stages: vertex, fragment, compute\n"
        << "Source languages: slang, hlsl, glsl, glsl-es, wgsl, msl, "
           "spirv, dxil, metallib, native\n"
        << "Assemble Truffle Shader Packages (TSP) from prepared variants, or "
           "compile through the optional shared Truffle::Shader adapter.\n"
        << "Assembly requires source-hash/compiler-name/compiler-version. "
           "Compilation computes source/dependency hashes and compiler identity; "
           "a supplied source-hash is verified. No toolchains are fetched.\n"
        << "--routes lists enabled source compilation, not package assembly "
           "or native GPU support. Target versions default to Vulkan 1.1/SPIR-V 1.3.\n";
}

[[nodiscard]] Result<Arguments> parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view key = argv[index];
        if (key == "--help") {
            usage(std::cout);
            return invalid("help requested");
        }
        if (key == "--compile") {
            result.compile = true;
            continue;
        }
        if (key == "--allow-experimental-metal") {
            result.experimentalMetal = true;
            continue;
        }
        if (index + 1 >= argc) {
            return invalid("missing value for " + std::string{key});
        }
        const std::string value = argv[++index];
        if (key == "--name") {
            result.name = value;
        } else if (key == "--target") {
            result.target = value;
        } else if (key == "--stage") {
            result.stage = value;
        } else if (key == "--entry") {
            result.entry = value;
        } else if (key == "--input") {
            result.input = value;
        } else if (key == "--output") {
            result.output = value;
        } else if (key == "--kind") {
            result.kind = value;
        } else if (key == "--source-hash") {
            result.sourceHash = value;
        } else if (key == "--source-language") {
            result.sourceLanguage = value;
        } else if (key == "--compiler-name") {
            result.compilerName = value;
        } else if (key == "--compiler-version") {
            result.compilerVersion = value;
        } else if (key == "--compiler-revision") {
            result.compilerRevision = value;
        } else if (key == "--append") {
            result.append = value;
        } else if (key == "--include-root") {
            if (value.empty()) return invalid("include root must not be empty");
            result.includeRoots.push_back(value);
        } else if (key == "--define") {
            const auto equals = value.find('=');
            result.defines.push_back({value.substr(0, equals),
                equals == std::string::npos ? "1" : value.substr(equals + 1)});
        } else if (key == "--vulkan-version") {
            result.targetSettings = true;
            if (value == truffle::shader::vulkan_version_name(truffle::shader::VulkanVersion::v1_0))
                result.vulkanTarget.environment = truffle::shader::VulkanVersion::v1_0;
            else if (value == truffle::shader::vulkan_version_name(truffle::shader::VulkanVersion::v1_1))
                result.vulkanTarget.environment = truffle::shader::VulkanVersion::v1_1;
            else return invalid("unknown Vulkan version: " + value);
        } else if (key == "--spirv-version") {
            result.targetSettings = true;
            if (value == truffle::shader::spirv_version_name(truffle::shader::SpirvVersion::v1_0))
                result.vulkanTarget.spirv = truffle::shader::SpirvVersion::v1_0;
            else if (value == truffle::shader::spirv_version_name(truffle::shader::SpirvVersion::v1_3))
                result.vulkanTarget.spirv = truffle::shader::SpirvVersion::v1_3;
            else return invalid("unknown SPIR-V version: " + value);
        } else if (key == "--hlsl-shader-model") {
            result.targetSettings = true;
            result.hlslModelSpecified = true;
            if (value != truffle::shader::hlsl_shader_model_name(result.hlslShaderModel))
                return invalid("unsupported HLSL shader model: " + value);
        } else if (key == "--msl-version" || key == "--glsl-version" || key == "--glsl-es-version") {
            result.targetSettings = true;
            const auto expected = key == "--msl-version" ? "2.1" : key == "--glsl-version" ? "410" : "300";
            if (value != expected) return invalid("unsupported native target version: " + value);
            const auto target = key == "--msl-version" ? "msl" : key == "--glsl-version" ? "glsl" : "glsl-es";
            // Validate after parsing so argument order has no effect.
            if (!result.nativeVersionTarget.empty() && result.nativeVersionTarget != target)
                return invalid("conflicting native version options");
            result.nativeVersionTarget = target;
        } else {
            return invalid("unknown argument " + std::string{key});
        }
    }
    if (result.name.empty() || result.target.empty() || result.stage.empty() ||
        result.input.empty() || result.output.empty() ||
        result.sourceLanguage.empty() ||
        (!result.compile &&
         (result.sourceHash.empty() || result.compilerName.empty() || result.compilerVersion.empty()))) {
        return invalid("required package arguments are missing");
    }
    if (!result.nativeVersionTarget.empty() && result.nativeVersionTarget != result.target)
        return invalid("native version option disagrees with target");
    if (result.hlslModelSpecified && result.sourceLanguage != "hlsl")
        return invalid("HLSL shader model requires HLSL source");
    if (!result.compile && (!result.includeRoots.empty() || !result.defines.empty() || result.targetSettings))
        return invalid("include roots, defines and target settings require --compile");
    if (result.compile && (!result.compilerName.empty() || !result.compilerVersion.empty() ||
                           !result.compilerRevision.empty()))
        return invalid("--compile derives compiler identity; do not override it");
    return result;
}

[[nodiscard]] Result<ShaderStage> parse_stage(std::string_view value) {
    if (value == "vertex") {
        return ShaderStage::vertex;
    }
    if (value == "fragment") {
        return ShaderStage::fragment;
    }
    if (value == "compute") {
        return ShaderStage::compute;
    }
    return invalid("unknown shader stage");
}

[[nodiscard]] Result<ShaderVariantKind> parse_kind(std::string_view value) {
    if (value == "precompiled") {
        return ShaderVariantKind::precompiled;
    }
    if (value == "native") {
        return ShaderVariantKind::native_override;
    }
    if (value == "generated") {
        return ShaderVariantKind::generated;
    }
    return invalid("unknown shader variant kind");
}

[[nodiscard]] ShaderByteFormat format_for_target(ShaderTarget target) {
    switch (target) {
    case ShaderTarget::spirv:
        return ShaderByteFormat::spirv;
    case ShaderTarget::dxil:
        return ShaderByteFormat::dxil;
    case ShaderTarget::metal_library:
        return ShaderByteFormat::metal_library;
    case ShaderTarget::metal_source:
    case ShaderTarget::wgsl:
    case ShaderTarget::glsl:
    case ShaderTarget::glsl_es:
        return ShaderByteFormat::native_source;
    }
    return ShaderByteFormat::native_source;
}

[[nodiscard]] Result<std::vector<std::byte>> read_file(
    const std::string& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return Status::failure(StatusCode::unavailable,
                               "could not open shader input");
    }
    std::vector<char> source{std::istreambuf_iterator<char>{input}, {}};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto value : source) {
        bytes.push_back(std::byte{static_cast<unsigned char>(value)});
    }
    return bytes;
}

[[nodiscard]] Status write_file(const std::string& path,
                                std::span<const std::byte> bytes) {
    std::ofstream output{path, std::ios::binary};
    if (!output) {
        return Status::failure(StatusCode::unavailable,
                               "could not open shader package output");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return Status::failure(StatusCode::backend_error,
                               "could not write shader package output");
    }
    return Status::success();
}

[[nodiscard]] int self_test() {
    ShaderPackageDesc desc;
    desc.name = "truffle-shaderc-self-test";
    desc.sources.push_back({
        .path = "self-test.spv",
        .language = truffle::rhi::ShaderSourceLanguage::spirv,
        .sha256 = std::string(64, '0'),
    });
    desc.compilers.push_back({
        .name = "truffle-shaderc",
        .version = "self-test",
        .revision = {},
    });
    ShaderVariantDesc variant;
    variant.target = ShaderTarget::spirv;
    variant.format = ShaderByteFormat::spirv;
    variant.kind = ShaderVariantKind::precompiled;
    variant.stage = ShaderStage::compute;
    variant.code = {std::byte{0x03}, std::byte{0x02}, std::byte{0x23},
                    std::byte{0x07}};
    desc.variants.push_back(std::move(variant));
    auto package = ShaderPackage::create(std::move(desc));
    if (!package.ok()) {
        return 1;
    }
    auto loaded = ShaderPackage::load(package.value().bytes());
    return loaded.ok() &&
                   loaded.value().find_variant(ShaderTarget::spirv, "main",
                                               ShaderStage::compute) != nullptr
               ? 0
               : 1;
}

} // namespace

int main(int argc, char** argv) {
    using namespace truffle;
    if (argc == 2 && std::string_view{argv[1]} == "--routes") {
        const auto routes = shader::Compiler::routes();
        std::cout << "source-routes=" << routes.size() << '\n';
        for (const auto& route : routes) {
            for (const auto stage : route.stages) {
                const auto print = [&](std::optional<shader::VulkanTarget> target) {
                    std::cout << "source=" << rhi::shader_source_language_name(route.language)
                              << " target=" << rhi::shader_target_name(route.target)
                              << " stage=" << (stage == ShaderStage::vertex ? "vertex" :
                                              stage == ShaderStage::fragment ? "fragment" : "compute");
                    if (target)
                        std::cout << " vulkan=" << shader::vulkan_version_name(target->environment)
                                  << " spirv=" << shader::spirv_version_name(target->spirv);
                    if (!route.metalTargets.empty()) std::cout << " msl=macos-2.1";
                    if (!route.glslTargets.empty()) std::cout << " glsl=410-core";
                    if (!route.glslEsTargets.empty()) std::cout << " glsl-es=300-es";
                    for (const auto model : route.hlslShaderModels)
                        std::cout << " hlsl-shader-model=" << shader::hlsl_shader_model_name(model);
                    if (route.experimental) std::cout << " experimental";
                    std::cout << '\n';
                };
                if (route.vulkanTargets.empty()) print(std::nullopt);
                else for (const auto target : route.vulkanTargets) print(target);
            }
        }
        return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
        return self_test();
    }
    if ((argc == 3 || (argc == 4 && std::string_view{argv[3]} == "--allow-experimental-metal")) &&
        std::string_view{argv[1]} == "--inspect") {
        auto bytesResult = read_file(argv[2]);
        if (!bytesResult.ok()) {
            std::cerr << bytesResult.status().message << '\n';
            return 2;
        }
        auto packageResult =
            ShaderPackage::load(std::move(bytesResult).value(),
                                {.allowExperimentalMetalGeneration = argc == 4});
        if (!packageResult.ok()) {
            std::cerr << packageResult.status().message << '\n';
            return 2;
        }
        const auto& desc = packageResult.value().desc();
        std::cout << desc.name << " variants=" << desc.variants.size()
                  << " compilers=" << desc.compilers.size()
                  << " sources=" << desc.sources.size()
                  << " configurations=" << desc.permutations.size() << '\n';
        for (const auto& compiler : desc.compilers) {
            std::cout << "compiler=" << compiler.name
                      << " version=" << compiler.version
                      << " revision=" << compiler.revision << '\n';
        }
        for (const auto& source : desc.sources) {
            std::cout << "source=" << source.path << " sha256=" << source.sha256 << '\n';
        }
        for (const auto& remap : desc.remaps) {
            std::cout << "remap=" << remap.group << ':' << remap.binding
                      << " element=" << remap.arrayElement
                      << " target=" << rhi::shader_target_name(remap.target)
                      << " stage=" << static_cast<unsigned>(remap.stage)
                      << " native=" << remap.nativeGroup << ':' << remap.nativeBinding
                      << ':' << remap.nativeArrayElement << '\n';
        }
        for (const auto& variant : desc.variants) {
            std::cout << "target=" << rhi::shader_target_name(variant.target)
                      << " entry=" << variant.entryPoint
                      << " native-entry=" << variant.nativeEntryPoint
                      << " permutation=" << variant.permutation
                      << " bindings=" << variant.reflection.bindings.size()
                      << " interfaces=" << variant.reflection.interfaces.size()
                      << " push-ranges=" << variant.reflection.pushConstants.size()
                      << " specializations=" << variant.reflection.specializationConstants.size()
                      << " workgroup=" << variant.reflection.requiredWorkgroupSize.width << 'x'
                      << variant.reflection.requiredWorkgroupSize.height << 'x'
                      << variant.reflection.requiredWorkgroupSize.depth << '\n';
            for (std::size_t axis = 0; axis < variant.reflection.workgroupSpecializationIds.size(); ++axis)
                if (const auto id = variant.reflection.workgroupSpecializationIds[axis])
                    std::cout << "workgroup-axis=" << "xyz"[axis] << " specialization-id=" << *id << '\n';
            for (const auto& binding : variant.reflection.bindings) {
                std::cout << "binding=" << binding.group << ':' << binding.binding
                          << " name=" << binding.name
                          << " type=" << static_cast<unsigned>(binding.type)
                          << " count=" << binding.arrayCount
                          << " minimum-size=" << binding.minimumSize
                          << " readonly=" << binding.readOnly
                          << " stage=" << static_cast<unsigned>(binding.stage) << '\n';
            }
            for (const auto& range : variant.reflection.pushConstants) {
                std::cout << "push-offset=" << range.offset << " size=" << range.size
                          << " stage=" << static_cast<unsigned>(range.stage) << '\n';
            }
            for (const auto& constant : variant.reflection.specializationConstants) {
                std::cout << "specialization-id=" << constant.id << " name=" << constant.name
                          << " type=" << static_cast<unsigned>(constant.type)
                          << " default-bits=" << constant.defaultValueBits << '\n';
            }
            for (const auto& value : variant.reflection.interfaces) {
                std::cout << "location=" << value.location << " name=" << value.name
                          << " direction=" << (value.direction == truffle::rhi::ShaderInterfaceDirection::input ? "input" : "output")
                          << " type=" << value.type << " interpolation=" << value.interpolation
                          << " stage=" << static_cast<unsigned>(value.stage) << '\n';
            }
        }
        return 0;
    }
    auto argumentsResult = parse_arguments(argc, argv);
    if (!argumentsResult.ok()) {
        if (!(argc == 2 && std::string_view{argv[1]} == "--help")) {
            std::cerr << argumentsResult.status().message << '\n';
            usage(std::cerr);
            return 2;
        }
        return 0;
    }
    const auto& arguments = argumentsResult.value();
    auto targetResult = rhi::shader_target_from_name(arguments.target);
    auto languageResult =
        rhi::shader_source_language_from_name(arguments.sourceLanguage);
    auto stageResult = parse_stage(arguments.stage);
    auto kindResult = parse_kind(arguments.kind);
    if (!targetResult.ok() || !languageResult.ok() || !stageResult.ok() ||
        !kindResult.ok()) {
        const auto* status = !targetResult.ok()   ? &targetResult.status()
                             : !languageResult.ok() ? &languageResult.status()
                             : !stageResult.ok()    ? &stageResult.status()
                                                    : &kindResult.status();
        std::cerr << status->message << '\n';
        return 2;
    }
    const auto target = targetResult.value();
    ShaderPackageDesc desc;
    if (!arguments.append.empty()) {
        auto baseBytes = read_file(arguments.append);
        if (!baseBytes.ok()) {
            std::cerr << baseBytes.status().message << '\n';
            return 2;
        }
        auto basePackage = ShaderPackage::load(std::move(baseBytes).value(),
            {.allowExperimentalMetalGeneration = arguments.experimentalMetal});
        if (!basePackage.ok()) {
            std::cerr << basePackage.status().message << '\n';
            return 2;
        }
        desc = basePackage.value().desc();
        if (desc.name != arguments.name) {
            std::cerr << "appended package name does not match\n";
            return 2;
        }
    } else {
        desc.name = arguments.name;
    }
    ShaderPackageDesc addition;
    addition.name = arguments.name;
    if (arguments.compile) {
        auto source = shader::read_source(arguments.input);
        if (!source.ok()) {
            std::cerr << source.status().message << '\n';
            return 2;
        }
        shader::CompileRequest request;
        request.name = arguments.name;
        request.source = std::move(source).value();
        request.stage = stageResult.value();
        request.entryPoint = arguments.entry;
        request.target = target;
        request.language = languageResult.value();
        request.vulkanTarget = arguments.vulkanTarget;
        request.hlslShaderModel = arguments.hlslShaderModel;
        request.allowExperimentalMetalGeneration = arguments.experimentalMetal;
        request.includeRoots = arguments.includeRoots;
        request.defines = arguments.defines;
        shader::Compiler compiler;
        auto compiled = compiler.compile(request);
        if (!compiled.ok()) {
            std::cerr << compiled.status().message << '\n';
            return 2;
        }
        if (!arguments.sourceHash.empty()) {
            const auto dependency = std::find_if(compiled.value().dependencies.begin(),
                compiled.value().dependencies.end(),
                [&](const auto& value) { return value.identity == request.source.identity; });
            if (dependency == compiled.value().dependencies.end() ||
                dependency->sha256 != arguments.sourceHash) {
                std::cerr << "--source-hash does not match the compiled root source\n";
                return 2;
            }
        }
        addition = compiled.value().package.desc();
        if (!compiled.value().diagnostics.empty())
            std::cerr << compiled.value().diagnostics << '\n';
    } else {
        auto code = read_file(arguments.input);
        if (!code.ok()) {
            std::cerr << code.status().message << '\n';
            return 2;
        }
        addition.sources.push_back({arguments.input, languageResult.value(), arguments.sourceHash});
        addition.compilers.push_back({arguments.compilerName, arguments.compilerVersion, arguments.compilerRevision});
        ShaderVariantDesc variant;
        variant.target = target;
        variant.format = format_for_target(target);
        variant.kind = kindResult.value();
        variant.stage = stageResult.value();
        variant.entryPoint = arguments.entry;
        variant.code = std::move(code).value();
        addition.variants.push_back(std::move(variant));
    }
    for (const auto& source : addition.sources) {
        const auto existing = std::find_if(desc.sources.begin(), desc.sources.end(),
            [&](const auto& value) { return value.path == source.path; });
        if (existing == desc.sources.end()) desc.sources.push_back(source);
        else if (*existing != source) {
            std::cerr << "appended source provenance does not match: " << source.path << '\n';
            return 2;
        }
    }
    for (const auto& compiler : addition.compilers) {
        if (std::find(desc.compilers.begin(), desc.compilers.end(), compiler) == desc.compilers.end())
            desc.compilers.push_back(compiler);
    }
    for (const auto& permutation : addition.permutations) {
        const auto existing = std::find_if(desc.permutations.begin(), desc.permutations.end(),
            [&](const auto& value) { return value.name == permutation.name; });
        if (existing == desc.permutations.end()) desc.permutations.push_back(permutation);
        else if (*existing != permutation) {
            std::cerr << "appended configuration does not match\n";
            return 2;
        }
    }
    desc.variants.insert(desc.variants.end(), addition.variants.begin(), addition.variants.end());
    for (const auto& remap : addition.remaps) {
        const auto existing = std::find_if(desc.remaps.begin(), desc.remaps.end(), [&](const auto& value) {
            return value.target == remap.target && value.stage == remap.stage &&
                   value.group == remap.group && value.binding == remap.binding &&
                   value.arrayElement == remap.arrayElement;
        });
        if (existing == desc.remaps.end()) desc.remaps.push_back(remap);
        else if (*existing != remap) {
            std::cerr << "appended native binding remap disagrees with the package\n";
            return 2;
        }
    }
    desc.diagnostics.insert(desc.diagnostics.end(), addition.diagnostics.begin(), addition.diagnostics.end());
    auto packageResult = ShaderPackage::create(std::move(desc),
        {.allowExperimentalMetalGeneration = arguments.experimentalMetal});
    if (!packageResult.ok()) {
        std::cerr << packageResult.status().message << '\n';
        return 2;
    }
    const auto status = write_file(arguments.output, packageResult.value().bytes());
    if (!status.ok()) {
        std::cerr << status.message << '\n';
        return 2;
    }
    std::cout << "wrote " << packageResult.value().bytes().size()
              << " bytes to " << arguments.output << '\n';
    return 0;
}

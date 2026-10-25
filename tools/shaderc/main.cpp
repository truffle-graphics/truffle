#include "truffle/rhi/shader_package.hpp"

#if defined(TRUFFLE_SHADERC_HAS_GLSLANG)
#include "glslang_compiler.hpp"
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
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
    bool compile = false;
};

[[nodiscard]] Status invalid(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

void usage(std::ostream& output) {
    output
        << "Usage: truffle-shaderc --name NAME --target TARGET --stage STAGE "
           "--input FILE --output FILE --source-hash SHA256 "
           "--source-language LANGUAGE "
           "--compiler-name NAME --compiler-version VERSION "
           "[--compiler-revision REV] [--entry NAME] "
           "[--kind precompiled|native|generated] [--append PACKAGE] "
           "[--compile]\n"
        << "Targets: spirv, dxil, metallib, msl, wgsl, glsl, glsl-es\n"
        << "Stages: vertex, fragment, compute\n"
        << "Source languages: slang, hlsl, glsl, glsl-es, wgsl, msl, "
           "spirv, dxil, metallib, native\n"
        << "The tool assembles deterministic packages from precompiled or "
           "backend-native variants; it does not fetch compiler toolchains.\n";
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
        } else {
            return invalid("unknown argument " + std::string{key});
        }
    }
    if (result.name.empty() || result.target.empty() || result.stage.empty() ||
        result.input.empty() || result.output.empty() ||
        result.sourceHash.empty() || result.sourceLanguage.empty() ||
        (!result.compile &&
         (result.compilerName.empty() || result.compilerVersion.empty()))) {
        return invalid("required package arguments are missing");
    }
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
    if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
        return self_test();
    }
    if (argc == 3 && std::string_view{argv[1]} == "--inspect") {
        auto bytesResult = read_file(argv[2]);
        if (!bytesResult.ok()) {
            std::cerr << bytesResult.status().message << '\n';
            return 2;
        }
        auto packageResult =
            ShaderPackage::load(std::move(bytesResult).value());
        if (!packageResult.ok()) {
            std::cerr << packageResult.status().message << '\n';
            return 2;
        }
        const auto& desc = packageResult.value().desc();
        std::cout << desc.name << " variants=" << desc.variants.size()
                  << " compilers=" << desc.compilers.size() << '\n';
        for (const auto& compiler : desc.compilers) {
            std::cout << "compiler=" << compiler.name
                      << " version=" << compiler.version
                      << " revision=" << compiler.revision << '\n';
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
    auto codeResult = read_file(arguments.input);
    if (!targetResult.ok() || !languageResult.ok() || !stageResult.ok() ||
        !kindResult.ok() || !codeResult.ok()) {
        const auto* status = !targetResult.ok()   ? &targetResult.status()
                             : !languageResult.ok() ? &languageResult.status()
                             : !stageResult.ok()    ? &stageResult.status()
                             : !kindResult.ok()     ? &kindResult.status()
                                                    : &codeResult.status();
        std::cerr << status->message << '\n';
        return 2;
    }
    const auto target = targetResult.value();
    if (arguments.compile &&
        (target != rhi::ShaderTarget::spirv ||
         (languageResult.value() != rhi::ShaderSourceLanguage::glsl &&
          languageResult.value() != rhi::ShaderSourceLanguage::glsl_es))) {
        std::cerr << "the enabled compiler route accepts GLSL or GLSL ES to "
                     "SPIR-V only\n";
        return 2;
    }
    ShaderPackageDesc desc;
    if (!arguments.append.empty()) {
        auto baseBytes = read_file(arguments.append);
        if (!baseBytes.ok()) {
            std::cerr << baseBytes.status().message << '\n';
            return 2;
        }
        auto basePackage = ShaderPackage::load(std::move(baseBytes).value());
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
    const rhi::ShaderSourceRecord source{
        .path = arguments.input,
        .language = languageResult.value(),
        .sha256 = arguments.sourceHash,
    };
    const auto existingSource =
        std::find_if(desc.sources.begin(), desc.sources.end(),
                     [&](const auto& candidate) {
                         return candidate.path == source.path;
                     });
    if (existingSource == desc.sources.end()) {
        desc.sources.push_back(source);
    } else if (*existingSource != source) {
        std::cerr << "appended source provenance does not match\n";
        return 2;
    }
    auto variantCode = std::move(codeResult).value();
    auto compilerName = arguments.compilerName;
    auto compilerVersion = arguments.compilerVersion;
    auto compilerRevision = arguments.compilerRevision;
    auto variantKind = kindResult.value();
    std::string compilerDiagnostics;
    if (arguments.compile) {
#if defined(TRUFFLE_SHADERC_HAS_GLSLANG)
        std::string sourceText(variantCode.size(), '\0');
        std::memcpy(sourceText.data(), variantCode.data(), variantCode.size());
        auto compiled = shaderc::compile_glsl_to_spirv(
            sourceText, stageResult.value(), arguments.entry,
            languageResult.value() == rhi::ShaderSourceLanguage::glsl_es);
        if (!compiled.ok()) {
            std::cerr << compiled.status().message << '\n';
            return 2;
        }
        variantCode = std::move(compiled.value().spirv);
        compilerName = "glslang";
        compilerVersion = std::move(compiled.value().compilerVersion);
        compilerRevision = std::move(compiled.value().compilerRevision);
        compilerDiagnostics = std::move(compiled.value().diagnostics);
        variantKind = rhi::ShaderVariantKind::generated;
#else
        std::cerr << "GLSL compilation support is not enabled; configure with "
                     "TRUFFLE_SHADERC_ENABLE_GLSLANG=ON\n";
        return 2;
#endif
    }
    const rhi::ShaderCompilerRecord compiler{
        .name = std::move(compilerName),
        .version = std::move(compilerVersion),
        .revision = std::move(compilerRevision),
    };
    if (std::find(desc.compilers.begin(), desc.compilers.end(), compiler) ==
        desc.compilers.end()) {
        desc.compilers.push_back(compiler);
    }
    ShaderVariantDesc variant;
    variant.target = target;
    variant.format = format_for_target(target);
    variant.kind = variantKind;
    variant.stage = stageResult.value();
    variant.entryPoint = arguments.entry;
    variant.code = std::move(variantCode);
    desc.variants.push_back(std::move(variant));
    if (!compilerDiagnostics.empty()) {
        desc.diagnostics.push_back({
            .severity = rhi::ShaderDiagnosticSeverity::info,
            .variant = arguments.target + ":" + arguments.stage + ":" +
                       arguments.entry,
            .message = std::move(compilerDiagnostics),
        });
    }
    auto packageResult = ShaderPackage::create(std::move(desc));
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

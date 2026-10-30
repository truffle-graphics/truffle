#include "truffle/shader/compiler.hpp"
#include "digest.hpp"
#include "compiler_inputs.hpp"
#include "spirv_reflection.hpp"
#include "spirv_translation.hpp"

#include "src/tint/lang/core/ir/transform/single_entry_point.h"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/spirv/writer/helpers/generate_bindings.h"
#include "src/tint/lang/spirv/writer/writer.h"
#include "src/tint/lang/wgsl/inspector/inspector.h"
#include "src/tint/lang/wgsl/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"

#include <algorithm>
#include <cstring>

namespace truffle::shader {
namespace {
rhi::Status invalid(std::string text) {
    return rhi::Status::failure(rhi::StatusCode::invalid_argument, std::move(text));
}
rhi::Status unsupported(std::string text) {
    return rhi::Status::failure(rhi::StatusCode::unsupported, std::move(text));
}
std::string diagnostic(const tint::Failure& failure) {
    return failure.reason.substr(0, 16384);
}
using detail::field;
} // namespace

rhi::Result<Compilation> compile_tint(const CompileRequest& request,
                                      std::vector<Compilation>& cache) {
    const bool spirvInput = request.language == rhi::ShaderSourceLanguage::spirv;
    if (request.name.empty() || request.source.identity.empty() ||
        request.source.text.empty() || request.entryPoint.empty() ||
        (!spirvInput && request.source.text.find('\0') != std::string::npos))
        return invalid("Tint requires a name, stable source identity, WGSL text and entry point");
    if (!request.defines.empty() || !request.includeRoots.empty() || request.resolver)
        return unsupported("WGSL has no preprocessor or native imports: defines, includeRoots and resolver are unsupported");
    if (request.generateDebugInfo)
        return unsupported("Tint adapter does not support generateDebugInfo");

    Compilation result;
    result.vulkanTarget = request.vulkanTarget;
    result.compiler = {"Tint", "chromium/7258", TRUFFLE_TINT_REVISION
        ";abseil=" TRUFFLE_ABSEIL_REVISION ";truffle-tint=1;spirv=1.3;vulkan=1.1;"
        "emit-point-size=0;spirv-tools=" TRUFFLE_SPIRV_TOOLS_REVISION
        ";spirv-headers=" TRUFFLE_SPIRV_HEADERS_REVISION};
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    if (request.target == rhi::ShaderTarget::metal_source)
        result.compiler.revision += detail::translation_provenance();
#endif
    result.dependencies.push_back({request.source.identity, detail::sha256(request.source.text)});
    std::string key;
    field(key, result.compiler.revision);
    field(key, request.name);
    field(key, request.source.identity);
    field(key, request.source.text);
    field(key, request.entryPoint);
    field(key, std::to_string(static_cast<int>(request.language)));
    field(key, std::to_string(static_cast<int>(request.stage)));
    field(key, std::to_string(static_cast<int>(request.target)));
    field(key, request.allowExperimentalMetalGeneration ? "metal-opt-in" : "no-metal");
    field(key, request.allowExperimentalWgslGeneration ? "wgsl-opt-in" : "no-wgsl");
    result.cacheKey = detail::sha256(key);
    for (const auto& entry : cache) {
        if (entry.cacheKey == result.cacheKey) {
            result = entry;
            result.cacheHit = true;
            return result;
        }
    }

    tint::Source::File source{request.source.identity, request.source.text};
    std::vector<uint32_t> inputWords;
    if (spirvInput) {
        if (request.source.text.size() % sizeof(uint32_t) != 0)
            return invalid("Tint SPIR-V input must contain whole 32-bit words");
        inputWords.resize(request.source.text.size() / sizeof(uint32_t));
        std::memcpy(inputWords.data(), request.source.text.data(), request.source.text.size());
        const auto validated = detail::validate_spirv(inputWords, request.vulkanTarget);
        if (!validated.ok()) return validated;
    }
    auto program = spirvInput ? tint::spirv::reader::Read(inputWords)
                              : tint::wgsl::reader::Parse(&source);
    if (!program.IsValid())
        return invalid("Tint WGSL validation failed: " + program.Diagnostics().Str().substr(0, 16384));
    tint::inspector::Inspector inspector{program};
    const auto entries = inspector.GetEntryPoints();
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
        return item.name == request.entryPoint;
    });
    if (entry == entries.end()) return invalid("Tint WGSL entry point not found: " + request.entryPoint);
    const auto stage = entry->stage == tint::inspector::PipelineStage::kVertex ? rhi::ShaderStage::vertex :
        entry->stage == tint::inspector::PipelineStage::kFragment ? rhi::ShaderStage::fragment : rhi::ShaderStage::compute;
    if (stage != request.stage) return invalid("Tint WGSL entry point stage does not match request");
    if (!entry->overrides.empty())
        return unsupported("WGSL pipeline overrides are not supported by this adapter; use literal constants");
    auto ir = tint::wgsl::reader::ProgramToLoweredIR(program);
    if (ir != tint::Success) return unsupported("Tint WGSL lowering failed: " + diagnostic(ir.Failure()));
    auto selected = tint::core::ir::transform::SingleEntryPoint(ir.Get(), request.entryPoint);
    if (selected != tint::Success) return invalid("Tint entry selection failed: " + diagnostic(selected.Failure()));

    // Both writers mutate IR: give WGSL its own selected lowering.
    std::string wgsl;
    if (request.target == rhi::ShaderTarget::wgsl) {
        auto wgslIR = tint::wgsl::reader::ProgramToLoweredIR(program);
        if (wgslIR != tint::Success) return unsupported(diagnostic(wgslIR.Failure()));
        auto wgslSelected = tint::core::ir::transform::SingleEntryPoint(wgslIR.Get(), request.entryPoint);
        if (wgslSelected != tint::Success) return invalid(diagnostic(wgslSelected.Failure()));
        auto output = tint::wgsl::writer::WgslFromIR(wgslIR.Get(), {});
        if (output != tint::Success) return unsupported("Tint WGSL generation failed: " + diagnostic(output.Failure()));
        wgsl = output->wgsl;
        tint::Source::File generated{"generated.wgsl", wgsl};
        auto validated = tint::wgsl::reader::Parse(&generated);
        if (!validated.IsValid())
            return invalid("Tint generated WGSL validation failed: " + validated.Diagnostics().Str());
    }
    tint::spirv::writer::Options options;
    options.emit_vertex_point_size = false;
    options.bindings = tint::spirv::writer::GenerateBindings(ir.Get());
    const auto canGenerate = tint::spirv::writer::CanGenerate(ir.Get(), options);
    if (canGenerate != tint::Success)
        return unsupported("Tint SPIR-V generation unsupported: " + diagnostic(canGenerate.Failure()));
    auto output = tint::spirv::writer::Generate(ir.Get(), options);
    if (output != tint::Success) return invalid("Tint SPIR-V generation failed: " + diagnostic(output.Failure()));
    const auto& words = output->spirv;
    auto status = detail::validate_spirv(words, request.vulkanTarget);
    if (!status.ok()) return status;
    rhi::ShaderVariantDesc variant;
    variant.target = request.target;
    variant.format = rhi::ShaderByteFormat::spirv;
    variant.kind = rhi::ShaderVariantKind::generated;
    variant.stage = stage;
    variant.entryPoint = request.entryPoint;
    status = detail::reflect_spirv(words, stage, variant.reflection);
    if (!status.ok()) return status;
    variant.code.resize(words.size() * sizeof(uint32_t));
    std::memcpy(variant.code.data(), words.data(), variant.code.size());
    rhi::ShaderPackageDesc desc;
    if (request.target == rhi::ShaderTarget::wgsl) {
        variant.format = rhi::ShaderByteFormat::native_source;
        variant.code.resize(wgsl.size());
        std::memcpy(variant.code.data(), wgsl.data(), wgsl.size());
    }
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    if (request.target == rhi::ShaderTarget::metal_source) {
        status = detail::translate_spirv(words, request, variant, desc.remaps);
        if (!status.ok()) return status;
    }
#endif
    desc.name = request.name;
    desc.compilers.push_back(result.compiler);
    desc.sources.push_back({request.source.identity, request.language, result.dependencies.front().sha256});
    desc.variants.push_back(std::move(variant));
    auto package = rhi::ShaderPackage::create(std::move(desc),
        {.allowExperimentalMetalGeneration = request.allowExperimentalMetalGeneration,
         .allowExperimentalWgslGeneration = request.allowExperimentalWgslGeneration});
    if (!package.ok()) return package.status();
    result.package = std::move(package).value();
    cache.push_back(result);
    return result;
}
} // namespace truffle::shader

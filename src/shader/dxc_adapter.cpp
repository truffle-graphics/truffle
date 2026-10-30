#include "truffle/shader/compiler.hpp"
#include "compiler_inputs.hpp"
#include "digest.hpp"
#include "dxil_reflection.hpp"
#include "spirv_reflection.hpp"
#include "spirv_translation.hpp"

#include <dxc/dxcapi.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <limits>
#include <map>
#include <memory>

namespace truffle::shader {
namespace {

using namespace detail::dxc;

rhi::Result<std::wstring> wide(IDxcUtils& utils, std::string_view text) {
    if (text.size() > std::numeric_limits<UINT32>::max())
        return invalid("UTF-8 argument is too large");
    IDxcBlobEncoding* raw = nullptr;
    auto status = utils.CreateBlob(text.data(), static_cast<UINT32>(text.size()), DXC_CP_UTF8, &raw);
    ComPtr<IDxcBlobEncoding> bytes{raw};
    if (FAILED(status) || !bytes) return invalid("Cannot encode UTF-8 argument");
    IDxcBlobWide* converted = nullptr;
    status = utils.GetBlobAsWide(bytes.get(), &converted);
    ComPtr<IDxcBlobWide> result{converted};
    if (FAILED(status) || !result) return invalid("Invalid UTF-8 argument");
    return std::wstring{result->GetStringPointer(), result->GetStringLength()};
}

class Includer final : public IDxcIncludeHandler {
public:
    Includer(const CompileRequest& request, IDxcUtils& utils) : request_(request), utils_(utils) {
        sources.emplace(request.source.identity, request.source.text);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDxcIncludeHandler))
            return E_NOINTERFACE;
        *value = static_cast<IDxcIncludeHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR filename, IDxcBlob** output) override {
        if (!filename || !output) return E_POINTER;
        *output = nullptr;
        IDxcBlobEncoding* raw = nullptr;
        auto status = utils_.CreateBlob(filename,
            static_cast<UINT32>(std::wcslen(filename) * sizeof(wchar_t)), DXC_CP_WIDE, &raw);
        ComPtr<IDxcBlobEncoding> encoded{raw};
        if (FAILED(status) || !encoded) {
            error = "Cannot encode include path";
            return E_FAIL;
        }
        IDxcBlobUtf8* utf8 = nullptr;
        status = utils_.GetBlobAsUtf8(encoded.get(), &utf8);
        ComPtr<IDxcBlobUtf8> path{utf8};
        if (FAILED(status) || !path) {
            error = "Invalid include path";
            return E_FAIL;
        }
        const std::string candidate{path->GetStringPointer(), path->GetStringLength()};
        auto found = resolved_.find(candidate);
        if (found == resolved_.end()) {
            if (frozen_) {
                error = "Include was not present in the preprocessed snapshot: " + candidate;
                return E_FAIL;
            }
            // DXC supplies a searched candidate path, not the original directive
            // or its includer; callers can implement a virtual path namespace.
            auto source = request_.resolver ? request_.resolver(candidate, {}, false)
                                            : read_source(candidate);
            if (!source.ok()) {
                error = source.status().message;
                return E_FAIL;
            }
            auto value = std::move(source).value();
            if (value.identity.empty() || value.text.find('\0') != std::string::npos ||
                value.text.size() > std::numeric_limits<UINT32>::max()) {
                error = "Invalid include identity/content: " + candidate;
                return E_FAIL;
            }
            const auto [saved, inserted] = sources.emplace(value.identity, value.text);
            if (!inserted && saved->second != value.text) {
                error = "Source changed during compilation: " + value.identity;
                return E_FAIL;
            }
            found = resolved_.emplace(candidate, std::move(value)).first;
        }
        IDxcBlobEncoding* blob = nullptr;
        status = utils_.CreateBlob(found->second.text.data(),
            static_cast<UINT32>(found->second.text.size()), DXC_CP_UTF8, &blob);
        if (FAILED(status) || !blob) {
            error = "Cannot create include source blob: " + candidate;
            return E_FAIL;
        }
        *output = blob;
        error.clear();
        return S_OK;
    }

    void freeze() { frozen_ = true; }
    std::map<std::string, std::string> sources;
    std::string error;

private:
    std::atomic<ULONG> references_{1};
    const CompileRequest& request_;
    IDxcUtils& utils_;
    std::map<std::string, Source> resolved_;
    bool frozen_ = false;
};

struct Attempt {
    ComPtr<IDxcResult> result;
    std::string diagnostics;
};

rhi::Result<Attempt> compile(IDxcCompiler3& compiler, const Source& source,
                             const std::vector<std::wstring>& arguments, Includer& includer) {
    std::vector<LPCWSTR> pointers;
    for (const auto& argument : arguments) pointers.push_back(argument.c_str());
    const DxcBuffer buffer{source.text.data(), source.text.size(), DXC_CP_UTF8};
    IDxcResult* raw = nullptr;
    const auto invoked = compiler.Compile(&buffer, pointers.data(),
        static_cast<UINT32>(pointers.size()), &includer, IID_PPV_ARGS(&raw));
    Attempt attempt{ComPtr<IDxcResult>{raw}, {}};
    if (FAILED(invoked) || !attempt.result)
        return invalid("Compiler invocation failed: " + std::to_string(invoked));
    if (attempt.result->HasOutput(DXC_OUT_ERRORS)) {
        IDxcBlobUtf8* rawErrors = nullptr;
        const auto retrieved = attempt.result->GetOutput(
            DXC_OUT_ERRORS, IID_PPV_ARGS(&rawErrors), nullptr);
        ComPtr<IDxcBlobUtf8> errors{rawErrors};
        if (FAILED(retrieved) || !errors) return invalid("Cannot retrieve compiler diagnostics");
        attempt.diagnostics.assign(errors->GetStringPointer(), errors->GetStringLength());
        attempt.diagnostics.resize(std::min(attempt.diagnostics.size(), std::size_t{16384}));
    }
    HRESULT status = E_FAIL;
    if (FAILED(attempt.result->GetStatus(&status)))
        return invalid("Cannot retrieve compiler status");
    if (FAILED(status))
        return invalid(attempt.diagnostics + (includer.error.empty() ? "" : "\n" + includer.error));
    return attempt;
}

template<class T> rhi::Result<ComPtr<T>> output(IDxcResult& result, DXC_OUT_KIND kind) {
    T* raw = nullptr;
    const auto status = result.GetOutput(kind, IID_PPV_ARGS(&raw), nullptr);
    ComPtr<T> blob{raw};
    if (FAILED(status) || !blob) return invalid("Compiler did not produce the requested output");
    return blob;
}

} // namespace

rhi::Result<Compilation> compile_dxc(const CompileRequest& request,
                                   std::vector<Compilation>& cache) {
    if (!request.sourceUnits.empty())
        return rhi::Status::failure(rhi::StatusCode::unsupported, "DXC does not support sourceUnits");
    if (request.name.empty() || request.source.identity.empty() || request.source.text.empty() ||
        request.entryPoint.empty() || request.source.text.find('\0') != std::string::npos ||
        request.source.identity.find('\0') != std::string::npos ||
        request.entryPoint.find('\0') != std::string::npos ||
        request.source.text.size() > std::numeric_limits<UINT32>::max())
        return invalid("Source identity/content, name and entry point are required");
    auto definesResult = detail::normalized_defines(request.defines);
    if (!definesResult.ok()) return definesResult.status();
    const auto defines = std::move(definesResult).value();
    auto utilsResult = instance<IDxcUtils>(CLSID_DxcUtils);
    if (!utilsResult.ok()) return utilsResult.status();
    const auto utils = std::move(utilsResult).value();
    auto compilerResult = instance<IDxcCompiler3>(CLSID_DxcCompiler);
    if (!compilerResult.ok()) return compilerResult.status();
    const auto compiler = std::move(compilerResult).value();
    ComPtr<Includer> includer{new Includer{request, *utils}};

    const auto sourceName = wide(*utils, request.source.identity);
    const auto entry = wide(*utils, request.entryPoint);
    if (!sourceName.ok()) return sourceName.status();
    if (!entry.ok()) return entry.status();
    const bool dxil = request.target == rhi::ShaderTarget::dxil;
    const std::wstring profile = request.stage == rhi::ShaderStage::vertex ? L"vs_6_0" :
                                 request.stage == rhi::ShaderStage::fragment ? L"ps_6_0" : L"cs_6_0";
    std::vector<std::wstring> arguments{
        sourceName.value(), L"-E", entry.value(), L"-T", profile, L"-HV", L"2021",
        L"-Od"};
    if (!dxil) {
        arguments.insert(arguments.end(), {L"-spirv", L"-fvk-use-gl-layout",
            request.vulkanTarget.environment == VulkanVersion::v1_0
                ? L"-fspv-target-env=vulkan1.0" : L"-fspv-target-env=vulkan1.1"});
    }
    for (const auto& root : request.includeRoots) {
        if (root.empty() || root.find('\0') != std::string::npos)
            return invalid("Include roots must be nonempty paths");
        auto value = wide(*utils, root);
        if (!value.ok()) return value.status();
        arguments.push_back(L"-I");
        arguments.push_back(std::move(value).value());
    }
    std::string configuration;
    for (const auto& define : defines) {
        const auto text = define.name + "=" + define.value;
        auto value = wide(*utils, text);
        if (!value.ok()) return value.status();
        arguments.push_back(L"-D");
        arguments.push_back(std::move(value).value());
        detail::field(configuration, define.name);
        detail::field(configuration, define.value);
    }
    if (request.generateDebugInfo) {
        arguments.push_back(L"-Zi");
        if (dxil) arguments.push_back(L"-Qembed_debug");
    }
    auto preprocessArguments = arguments;
    preprocessArguments.push_back(L"-P");
    auto preprocessResult = compile(*compiler, request.source, preprocessArguments, *includer);
    if (!preprocessResult.ok()) return preprocessResult.status();
    auto preprocessed = std::move(preprocessResult).value();
    auto text = output<IDxcBlobUtf8>(*preprocessed.result, DXC_OUT_HLSL);
    if (!text.ok()) return text.status();

    Compilation result;
    result.vulkanTarget = request.vulkanTarget;
    IDxcVersionInfo* rawVersion = nullptr;
    const auto versionStatus = compiler->QueryInterface(IID_PPV_ARGS(&rawVersion));
    ComPtr<IDxcVersionInfo> version{rawVersion};
    UINT32 major = 0, minor = 0;
    if (FAILED(versionStatus) || !version || FAILED(version->GetVersion(&major, &minor)))
        return invalid("Compiler version information unavailable");
    result.compiler = {"dxc", std::to_string(major) + "." + std::to_string(minor),
        TRUFFLE_DXC_REVISION ";truffle-dxc=1;hlsl=2021;shader-model=6.0;" +
        (dxil ? std::string{"dxil;validator=dxc"} :
            "gl-layout;vulkan=" + std::string{vulkan_version_name(request.vulkanTarget.environment)} +
            ";spirv=" + std::string{spirv_version_name(request.vulkanTarget.spirv)} +
            ";spirv-tools=" TRUFFLE_SPIRV_TOOLS_REVISION)};
    result.compiler.revision += ";target=" + std::string{rhi::shader_target_name(request.target)} +
        ";debug=" + (request.generateDebugInfo ? "1" : "0") +
        ";experimental-metal=" + (request.allowExperimentalMetalGeneration ? "1" : "0");
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    if (!dxil && request.target != rhi::ShaderTarget::spirv)
        result.compiler.revision += detail::translation_provenance();
#endif
    std::string key;
    for (const auto& value : {request.name, request.source.identity, request.entryPoint,
                             result.compiler.name, result.compiler.version, result.compiler.revision,
                             std::to_string(static_cast<unsigned int>(request.stage)),
                             std::to_string(static_cast<unsigned int>(request.target)),
                             std::to_string(request.generateDebugInfo), configuration})
        detail::field(key, value);
    detail::field(key, std::string_view{text.value()->GetStringPointer(), text.value()->GetStringLength()});
    for (const auto& root : request.includeRoots) detail::field(key, root);
    for (const auto& [identity, content] : includer->sources) {
        result.dependencies.push_back({identity, detail::sha256(content)});
        detail::field(key, identity);
        detail::field(key, content);
    }
    result.cacheKey = detail::sha256(key);
    for (const auto& previous : cache) {
        if (previous.cacheKey == result.cacheKey) {
            result = previous;
            result.cacheHit = true;
            return result;
        }
    }
    includer->freeze();
    auto attemptResult = compile(*compiler, request.source, arguments, *includer);
    if (!attemptResult.ok()) return attemptResult.status();
    auto attempt = std::move(attemptResult).value();
    auto code = output<IDxcBlob>(*attempt.result, DXC_OUT_OBJECT);
    if (!code.ok()) return code.status();
    rhi::ShaderVariantDesc variant;
    variant.target = request.target;
    variant.format = dxil ? rhi::ShaderByteFormat::dxil : rhi::ShaderByteFormat::spirv;
    variant.kind = rhi::ShaderVariantKind::generated;
    variant.stage = request.stage;
    variant.entryPoint = request.entryPoint;
    rhi::ShaderPackageDesc desc;
    if (dxil) {
        const auto validation = detail::validate_dxil(*code.value());
        if (!validation.ok()) return validation;
        const auto reflected = detail::reflect_dxil(*utils, *code.value(), request.stage, variant.reflection);
        if (!reflected.ok()) return reflected;
    } else {
        if (code.value()->GetBufferSize() % sizeof(unsigned int) != 0)
            return invalid("SPIR-V output has invalid byte size");
        std::vector<unsigned int> words(code.value()->GetBufferSize() / sizeof(unsigned int));
        std::memcpy(words.data(), code.value()->GetBufferPointer(), code.value()->GetBufferSize());
        const auto validation = detail::validate_spirv(words, request.vulkanTarget);
        if (!validation.ok()) return validation;
        const auto reflected = detail::reflect_spirv(words, request.stage, variant.reflection);
        if (!reflected.ok()) return reflected;
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
        if (request.target != rhi::ShaderTarget::spirv) {
            const auto translated = detail::translate_spirv(words, request, variant, desc.remaps);
            if (!translated.ok()) return translated;
            if (request.target == rhi::ShaderTarget::glsl || request.target == rhi::ShaderTarget::glsl_es) {
                const auto validation = detail::validate_generated_glsl(request, variant.code);
                if (!validation.ok()) return validation;
            }
        }
#endif
    }
    if (dxil || request.target == rhi::ShaderTarget::spirv) {
        variant.code.resize(code.value()->GetBufferSize());
        std::memcpy(variant.code.data(), code.value()->GetBufferPointer(), variant.code.size());
    }
    result.diagnostics = std::move(attempt.diagnostics);
    desc.name = request.name;
    desc.compilers.push_back(result.compiler);
    for (const auto& dependency : result.dependencies)
        desc.sources.push_back({dependency.identity, request.language, dependency.sha256});
    if (!defines.empty()) {
        variant.permutation = detail::sha256(configuration);
        desc.permutations.push_back({variant.permutation, defines, {}});
    }
    if (!result.diagnostics.empty())
        desc.diagnostics.push_back({rhi::ShaderDiagnosticSeverity::info,
            std::string{rhi::shader_target_name(request.target)} + ":" +
                std::to_string(static_cast<int>(request.stage)) + ":" +
                request.entryPoint + ":" + variant.permutation,
            result.diagnostics.substr(0, 4096)});
    desc.variants.push_back(std::move(variant));
    auto package = rhi::ShaderPackage::create(std::move(desc),
        {.allowExperimentalMetalGeneration = request.allowExperimentalMetalGeneration});
    if (!package.ok()) return package.status();
    result.package = std::move(package).value();
    cache.push_back(result);
    return result;
}

} // namespace truffle::shader

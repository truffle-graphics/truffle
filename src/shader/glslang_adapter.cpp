#include "truffle/shader/compiler.hpp"
#include "digest.hpp"
#include "spirv_reflection.hpp"
#include "spirv_translation.hpp"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Include/Types.h>
#include <glslang/MachineIndependent/localintermediate.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Include/glslang_c_interface.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace truffle::shader {
namespace {

rhi::Status invalid(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::invalid_argument, std::move(message));
}

rhi::Status unsupported(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::unsupported, std::move(message));
}

std::string bounded(const char* log) {
    return log ? std::string{log}.substr(0, 16384) : std::string{};
}

// One process lifetime, not one initialize/finalize pair per runtime request.
// Serialize adapter entry as a conservative contract across pinned glslang builds.
struct Process {
    std::mutex mutex;
    bool initialized = glslang::InitializeProcess();
    ~Process() {
        if (initialized) glslang::FinalizeProcess();
    }
};

class Includer final : public glslang::TShader::Includer {
public:
    explicit Includer(const CompileRequest& request) : request_(request) {
        sources.emplace(request.source.identity, request.source.text);
        active_.insert(request.source.identity);
    }

    IncludeResult* includeLocal(const char* name, const char* parent, std::size_t depth) override {
        return resolve(name, parent, false, depth);
    }
    IncludeResult* includeSystem(const char* name, const char* parent, std::size_t depth) override {
        return resolve(name, parent, true, depth);
    }
    void releaseInclude(IncludeResult* result) override {
        if (result) {
            active_.erase(result->headerName);
            delete result;
        }
    }

    std::map<std::string, std::string> sources;
    std::string error;
    void freeze() { frozen_ = true; }

private:
    IncludeResult* resolve(const char* name, const char* parent, bool system, std::size_t depth) {
        if (depth > 64) {
            error = "Include depth exceeds 64: " + std::string{name};
            return nullptr;
        }
        auto source = load(name, parent, system);
        if (!source.ok()) {
            error = source.status().message;
            return nullptr;
        }
        auto resolved = std::move(source).value();
        if (resolved.identity.empty() || resolved.text.find('\0') != std::string::npos) {
            error = "Invalid include identity/content: " + std::string{name};
            return nullptr;
        }
        if (active_.contains(resolved.identity)) {
            error = "Cyclic shader include: " + resolved.identity;
            return nullptr;
        }
        const auto [it, inserted] = sources.emplace(resolved.identity, resolved.text);
        if (!inserted && it->second != resolved.text) {
            error = "Source changed during compilation: " + resolved.identity;
            return nullptr;
        }
        active_.insert(it->first);
        return new IncludeResult{it->first, it->second.data(), it->second.size(), nullptr};
    }

    rhi::Result<Source> load(const char* name, const char* parent, bool system) {
        const auto key = std::make_tuple(std::string{name}, std::string{parent}, system);
        const auto found = resolved_.find(key);
        if (found != resolved_.end()) return found->second;
        if (frozen_) {
            return invalid("Include was not present in the preprocessed snapshot: " + std::string{name});
        }
        auto result = load_current(name, parent, system);
        if (result.ok()) resolved_.insert_or_assign(key, result.value());
        return result;
    }

    rhi::Result<Source> load_current(const char* name, const char* parent, bool system) {
        if (request_.resolver) return request_.resolver(name, parent, system);
        if (!system) {
            auto result = read_source((std::filesystem::path{parent}.parent_path() / name).string());
            if (result.ok()) return result;
        }
        for (const auto& root : request_.includeRoots) {
            auto result = read_source((std::filesystem::path{root} / name).string());
            if (result.ok()) return result;
        }
        return invalid("Missing shader include '" + std::string{name} + "' from " + parent);
    }

    const CompileRequest& request_;
    std::set<std::string> active_;
    std::map<std::tuple<std::string, std::string, bool>, Source> resolved_;
    bool frozen_ = false;
};

bool identifier(std::string_view value) {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

void field(std::string& output, std::string_view value) {
    output += std::to_string(value.size()) + ":";
    output += value;
}

} // namespace

rhi::Result<Compilation> compile_glslang(const CompileRequest& request,
                                       std::vector<Compilation>& cache) {
    if (request.source.identity.empty() || request.source.text.empty() ||
        request.source.text.find('\0') != std::string::npos ||
        request.source.text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        request.entryPoint != "main" || request.name.empty()) {
        return invalid("Source identity/content/name required; GLSL entry point must be main");
    }
    EShLanguage stage;
    switch (request.stage) {
    case rhi::ShaderStage::vertex: stage = EShLangVertex; break;
    case rhi::ShaderStage::fragment: stage = EShLangFragment; break;
    case rhi::ShaderStage::compute: stage = EShLangCompute; break;
    default: return unsupported("Unsupported shader stage");
    }
    auto defines = request.defines;
    std::sort(defines.begin(), defines.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    std::string preamble = "#extension GL_GOOGLE_include_directive : enable\n";
    for (std::size_t i = 0; i < defines.size(); ++i) {
        const auto& define = defines[i];
        if (!identifier(define.name) || define.value.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            (i && defines[i-1].name == define.name)) {
            return invalid("Invalid/duplicate shader define: " + define.name);
        }
        preamble += "#define " + define.name + " " + define.value + "\n";
    }
    static Process process;
    const std::lock_guard lock{process.mutex};
    if (!process.initialized) return unsupported("glslang initialization failed");
    glslang_version_t version{};
    glslang_get_version(&version);
    Compilation result;
    result.vulkanTarget = request.vulkanTarget;
    result.compiler = {"glslang", std::to_string(version.major) + "." + std::to_string(version.minor) +
                                   "." + std::to_string(version.patch), TRUFFLE_GLSLANG_REVISION};
    result.compiler.revision += ";truffle-glslang=5;vulkan=" +
        std::string{vulkan_version_name(request.vulkanTarget.environment)} + ";spirv=" +
        std::string{spirv_version_name(request.vulkanTarget.spirv)};
    result.compiler.revision += ";target=" + std::string{rhi::shader_target_name(request.target)} +
        ";debug=" + (request.generateDebugInfo ? "1" : "0") +
        ";experimental-metal=" + (request.allowExperimentalMetalGeneration ? "1" : "0");
#if defined(TRUFFLE_SHADER_HAS_SPIRV_TOOLS)
    result.compiler.revision += ";spirv-tools=" TRUFFLE_SPIRV_TOOLS_REVISION
                                ";spirv-headers=" TRUFFLE_SPIRV_HEADERS_REVISION;
#endif
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    if (request.target != rhi::ShaderTarget::spirv) {
        result.compiler.revision += ";spirv-cross=" TRUFFLE_SPIRV_CROSS_REVISION
            ";truffle-cross=2;msl=macos-2.1;glsl=410-core;essl=300-es;"
            "flat-scalar-resources;gl-fixup-clipspace;metal-vertex-reserve=16;"
            "metal-no-auxiliary-bindings;"
            "cross-namespace=truffle_spirv_cross;cross-spv-namespace=truffle_spirv_cross_spv;cross-exceptions";
    }
#endif
    glslang::TShader shader{stage};
    const char* text = request.source.text.c_str();
    const char* identity = request.source.identity.c_str();
    const int length = static_cast<int>(request.source.text.size());
    shader.setStringsWithLengthsAndNames(&text, &length, &identity, 1);
    shader.setPreamble(preamble.c_str());
    shader.setEntryPoint("main");
    const bool es = request.language == rhi::ShaderSourceLanguage::glsl_es;
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, es ? 310 : 450);
    shader.setEnvClient(glslang::EShClientVulkan,
        request.vulkanTarget.environment == VulkanVersion::v1_0
            ? glslang::EShTargetVulkan_1_0 : glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,
        request.vulkanTarget.spirv == SpirvVersion::v1_0
            ? glslang::EShTargetSpv_1_0 : glslang::EShTargetSpv_1_3);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    Includer includer{request};
    std::string preprocessed;
    if (!shader.preprocess(GetDefaultResources(), es ? 310 : 450, es ? EEsProfile : ECoreProfile,
                           false, false, messages, &preprocessed, includer)) {
        return invalid("glslang preprocessing failed: " + includer.error + "\n" + bounded(shader.getInfoLog()));
    }
    if (!includer.error.empty()) {
        // A failed local lookup can legitimately fall back to a successful system lookup.
        // glslang's successful preprocessing is authoritative.
        includer.error.clear();
    }
    std::string key;
    field(key, "Truffle GLSL adapter 3; default-resource-limits");
    field(key, result.compiler.version);
    field(key, result.compiler.revision);
    field(key, request.name);
    field(key, request.entryPoint);
    field(key, std::to_string(static_cast<int>(request.stage)));
    field(key, std::to_string(static_cast<int>(request.language)));
    field(key, std::to_string(static_cast<int>(request.target)));
    field(key, request.generateDebugInfo ? "debug" : "release");
    field(key, request.allowExperimentalMetalGeneration ? "experimental-metal" : "stable");
    field(key, preamble);
    field(key, preprocessed);
    for (const auto& root : request.includeRoots) field(key, root);
    for (const auto& [name, content] : includer.sources) {
        result.dependencies.push_back({name, detail::sha256(content)});
        field(key, name);
        field(key, content);
    }
    result.cacheKey = detail::sha256(key);
    for (const auto& entry : cache) {
        if (entry.cacheKey == result.cacheKey) {
            result = entry;
            result.cacheHit = true;
            return result;
        }
    }
    includer.freeze();
    if (!shader.parse(GetDefaultResources(), es ? 310 : 450, es ? EEsProfile : ECoreProfile,
                      false, false, messages, includer)) {
        return invalid("glslang parsing failed: " + includer.error + "\n" + bounded(shader.getInfoLog()));
    }
    if ((shader.getIntermediate()->getProfile() == EEsProfile) != es) {
        return invalid("GLSL source profile does not match the requested language");
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) return invalid("glslang linking failed: " + bounded(program.getInfoLog()));
    rhi::ShaderVariantDesc variant;
    variant.target = request.target;
    variant.format = rhi::ShaderByteFormat::spirv;
    variant.kind = rhi::ShaderVariantKind::generated;
    variant.stage = request.stage;
    variant.entryPoint = request.entryPoint;
    std::vector<unsigned int> words;
    glslang::SpvOptions options;
    options.generateDebugInfo = request.generateDebugInfo;
    options.disableOptimizer = true;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &logger, &options);
    if (words.empty() || words[0] != 0x07230203) return invalid("glslang SPIR-V generation failed");
    auto status = detail::reflect_spirv(words, request.stage, variant.reflection);
    if (!status.ok()) return status;
#if defined(TRUFFLE_SHADER_HAS_SPIRV_TOOLS)
    const auto validation = detail::validate_spirv(words, request.vulkanTarget);
    if (!validation.ok()) return validation;
#endif
    variant.code.resize(words.size() * sizeof(unsigned int));
    std::memcpy(variant.code.data(), words.data(), variant.code.size());
    result.diagnostics = (bounded(shader.getInfoLog()) + bounded(program.getInfoLog()) +
                          logger.getAllMessages()).substr(0, 16384);
    rhi::ShaderPackageDesc desc;
#if defined(TRUFFLE_SHADER_HAS_SPIRV_CROSS)
    if (request.target != rhi::ShaderTarget::spirv) {
        auto translated = detail::translate_spirv(words, request, variant, desc.remaps);
        if (!translated.ok()) return translated;
        if (request.target == rhi::ShaderTarget::glsl || request.target == rhi::ShaderTarget::glsl_es) {
            glslang::TShader generated{stage};
            const char* generatedText = reinterpret_cast<const char*>(variant.code.data());
            const int generatedLength = static_cast<int>(variant.code.size());
            generated.setStringsWithLengths(&generatedText, &generatedLength, 1);
            const bool outputEs = request.target == rhi::ShaderTarget::glsl_es;
            if (!generated.parse(GetDefaultResources(), outputEs ? 300 : 410,
                                 outputEs ? EEsProfile : ECoreProfile, false, false, EShMsgDefault))
                return invalid("Generated GLSL validation failed: " + bounded(generated.getInfoLog()));
            glslang::TProgram generatedProgram;
            generatedProgram.addShader(&generated);
            if (!generatedProgram.link(EShMsgDefault))
                return invalid("Generated GLSL link validation failed: " + bounded(generatedProgram.getInfoLog()));
        }
    }
#endif
    desc.name = request.name;
    desc.compilers.push_back(result.compiler);
    for (const auto& dependency : result.dependencies)
        desc.sources.push_back({dependency.identity, request.language, dependency.sha256});
    if (!defines.empty()) {
        variant.permutation = detail::sha256(preamble);
        desc.permutations.push_back({variant.permutation, defines, {}});
    }
    if (!result.diagnostics.empty()) {
        desc.diagnostics.push_back({
            rhi::ShaderDiagnosticSeverity::info,
            std::string{rhi::shader_target_name(request.target)} + ":" +
                std::to_string(static_cast<int>(request.stage)) + ":" +
                request.entryPoint + ":" + variant.permutation,
            result.diagnostics.substr(0, 4096)});
    }
    desc.variants.push_back(std::move(variant));
    auto package = rhi::ShaderPackage::create(std::move(desc),
        {.allowExperimentalMetalGeneration = request.allowExperimentalMetalGeneration});
    if (!package.ok()) return package.status();
    result.package = std::move(package).value();
    cache.push_back(result);
    return result;
}

} // namespace truffle::shader

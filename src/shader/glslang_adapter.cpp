#include "truffle/shader/compiler.hpp"
#include "digest.hpp"

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

rhi::Status reflect(glslang::TProgram& program, EShLanguage nativeStage, rhi::ShaderStage stage,
                    rhi::ShaderReflection& reflection, std::vector<StageInterface>& interfaces) {
    if (!program.buildReflection(EShReflectionSeparateBuffers | EShReflectionAllBlockVariables |
                                 EShReflectionIntermediateIO | EShReflectionAllIOVariables)) {
        return unsupported("glslang could not reflect this shader interface");
    }
    const auto addBlock = [&](const glslang::TObjectReflection& block, bool storage) -> rhi::Status {
        const auto& type = *block.getType();
        const auto& q = type.getQualifier();
        if (q.layoutPushConstant) {
            return unsupported("Push-constant reflection is not supported by the initial GLSL adapter");
        }
        if (!q.hasBinding() || !q.hasSet() || type.isArray() || block.size <= 0) {
            return unsupported("Buffer requires explicit set/binding, a known minimum size and no descriptor array: " + block.name);
        }
        reflection.bindings.push_back({
            .name = block.name, .stage = stage, .type = rhi::ResourceBindingType::buffer,
            .group = static_cast<std::uint32_t>(q.layoutSet), .binding = static_cast<std::uint32_t>(q.layoutBinding), .arrayCount = 1,
            .minimumSize = static_cast<std::size_t>(block.size),
            .readOnly = !storage || q.readonly});
        return rhi::Status::success();
    };
    // Reflect linked declarations, including inactive resources that glslang
    // still emits into SPIR-V, rather than only its live-uniform database.
    auto* tree = program.getIntermediate(nativeStage)->getTreeRoot()->getAsAggregate();
    for (auto* node : tree->getSequence()) {
        auto* aggregate = node->getAsAggregate();
        if (!aggregate || aggregate->getOp() != glslang::EOpLinkerObjects) continue;
        for (auto* declaration : aggregate->getSequence()) {
        auto* symbol = declaration->getAsSymbolNode();
        if (!symbol) continue;
        const auto& type = symbol->getType();
        const auto& q = type.getQualifier();
        const std::string name = symbol->getName().c_str();
        if (q.storage != glslang::EvqUniform && q.storage != glslang::EvqBuffer) continue;
        if (type.getBasicType() == glslang::EbtBlock) {
            const glslang::TObjectReflection block{name, type, 0, 0,
                glslang::TIntermediate::getBlockSize(type), -1};
            auto status = addBlock(block, q.storage == glslang::EvqBuffer);
            if (!status.ok()) return status;
            continue;
        }
        if (type.getBasicType() != glslang::EbtSampler || !q.hasBinding() || !q.hasSet() ||
            type.isArrayOfArrays() || type.containsUnsizedArray()) {
            return unsupported("Unsupported uniform reflection: " + name);
        }
        const auto& sampler = type.getSampler();
        if (sampler.isCombined() || sampler.isSubpass()) {
            return unsupported("Combined samplers/subpass inputs are not representable by the initial adapter: " + name);
        }
        reflection.bindings.push_back({
            .name = name, .stage = stage,
            .type = sampler.isPureSampler() ? rhi::ResourceBindingType::sampler : rhi::ResourceBindingType::texture,
            .group = static_cast<std::uint32_t>(q.layoutSet), .binding = static_cast<std::uint32_t>(q.layoutBinding),
            .arrayCount = static_cast<std::uint32_t>(type.isArray() ? type.getOuterArraySize() : 1),
            .minimumSize = 0, .readOnly = !sampler.isImage() || q.readonly});
        }
    }
    if (program.getNumAtomicCounters() != 0) return unsupported("Atomic counter reflection is unsupported");
    const auto addIO = [&](const glslang::TObjectReflection& io, bool input) -> rhi::Status {
        const auto& type = *io.getType();
        if (type.containsBuiltIn() || io.name.starts_with("gl_")) return rhi::Status::success();
        const auto& q = type.getQualifier();
        if (!q.hasLocation() || type.isArray() || type.isStruct() || type.isMatrix() ||
            (type.getBasicType() != glslang::EbtFloat && type.getBasicType() != glslang::EbtInt &&
             type.getBasicType() != glslang::EbtUint) ||
            q.layoutComponent != glslang::TQualifier::layoutComponentEnd) {
            return unsupported("Interface requires a located 32-bit scalar/vector: " + io.name);
        }
        rhi::ShaderInterfaceVariable variable{
            .stage = stage, .location = q.layoutLocation, .name = io.name,
            .type = type.getVectorSize() == 1
                ? (type.getBasicType() == glslang::EbtFloat ? "float" : type.getBasicType() == glslang::EbtInt ? "int" : "uint")
                : std::string{type.getBasicType() == glslang::EbtFloat ? "vec" : type.getBasicType() == glslang::EbtInt ? "ivec" : "uvec"} + std::to_string(type.getVectorSize()),
            .interpolation = q.flat ? "flat" : q.nopersp ? "noperspective" : "smooth"};
        if (q.centroid) variable.interpolation = "centroid " + variable.interpolation;
        if (q.sample) variable.interpolation = "sample " + variable.interpolation;
        interfaces.push_back({input, variable});
        // TSP 1.0 has no direction field: its interfaces describe pipeline edges.
        if ((stage == rhi::ShaderStage::vertex && input) ||
            (stage == rhi::ShaderStage::fragment && !input)) {
            reflection.interfaces.push_back(std::move(variable));
        }
        return rhi::Status::success();
    };
    for (int i = 0; i < program.getNumPipeInputs(); ++i) {
        auto status = addIO(program.getPipeInput(i), true);
        if (!status.ok()) return status;
    }
    for (int i = 0; i < program.getNumPipeOutputs(); ++i) {
        auto status = addIO(program.getPipeOutput(i), false);
        if (!status.ok()) return status;
    }
    if (stage == rhi::ShaderStage::compute) {
        reflection.requiredWorkgroupSize = {program.getLocalSize(0), program.getLocalSize(1), program.getLocalSize(2)};
        reflection.preferredWorkgroupSize = reflection.requiredWorkgroupSize;
    }
    return rhi::Status::success();
}

} // namespace

rhi::Result<Compilation> compile_glslang(const CompileRequest& request,
                                       std::vector<Compilation>& cache) {
    if (request.target != rhi::ShaderTarget::spirv ||
        (request.language != rhi::ShaderSourceLanguage::glsl &&
         request.language != rhi::ShaderSourceLanguage::glsl_es)) {
        return unsupported("Only GLSL/GLSL ES to Vulkan 1.1 SPIR-V 1.3 is supported");
    }
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
    result.compiler = {"glslang", std::to_string(version.major) + "." + std::to_string(version.minor) +
                                   "." + std::to_string(version.patch), TRUFFLE_GLSLANG_REVISION};
    glslang::TShader shader{stage};
    const char* text = request.source.text.c_str();
    const char* identity = request.source.identity.c_str();
    const int length = static_cast<int>(request.source.text.size());
    shader.setStringsWithLengthsAndNames(&text, &length, &identity, 1);
    shader.setPreamble(preamble.c_str());
    shader.setEntryPoint("main");
    const bool es = request.language == rhi::ShaderSourceLanguage::glsl_es;
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, es ? 310 : 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
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
    field(key, "Truffle GLSL adapter 1; Vulkan1.1; SPIRV1.3; default-resource-limits");
    field(key, result.compiler.version);
    field(key, result.compiler.revision);
    field(key, request.name);
    field(key, request.entryPoint);
    field(key, std::to_string(static_cast<int>(request.stage)));
    field(key, std::to_string(static_cast<int>(request.language)));
    field(key, std::to_string(static_cast<int>(request.target)));
    field(key, request.generateDebugInfo ? "debug" : "release");
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
    auto status = reflect(program, stage, request.stage, variant.reflection, result.interfaces);
    if (!status.ok()) return status;
    std::vector<unsigned int> words;
    glslang::SpvOptions options;
    options.generateDebugInfo = request.generateDebugInfo;
    options.disableOptimizer = true;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &logger, &options);
    if (words.empty() || words[0] != 0x07230203) return invalid("glslang SPIR-V generation failed");
    // Specialization constants (including local_size_*_id) need richer reflection.
    for (std::size_t i = 5; i < words.size();) {
        const auto count = words[i] >> 16;
        const auto opcode = words[i] & 0xffff;
        if (count == 0 || count > words.size() - i) return invalid("Malformed generated SPIR-V");
        if (opcode >= 48 && opcode <= 52) return unsupported("Specialization constant reflection is unsupported");
        i += count;
    }
    variant.code.resize(words.size() * sizeof(unsigned int));
    std::memcpy(variant.code.data(), words.data(), variant.code.size());
    result.diagnostics = bounded(shader.getInfoLog()) + bounded(program.getInfoLog()) + logger.getAllMessages().substr(0, 16384);
    rhi::ShaderPackageDesc desc;
    desc.name = request.name;
    desc.compilers.push_back(result.compiler);
    for (const auto& dependency : result.dependencies)
        desc.sources.push_back({dependency.identity, request.language, dependency.sha256});
    if (!defines.empty()) {
        variant.permutation = detail::sha256(preamble);
        desc.permutations.push_back({variant.permutation, defines, {}});
    }
    desc.variants.push_back(std::move(variant));
    auto package = rhi::ShaderPackage::create(std::move(desc));
    if (!package.ok()) return package.status();
    result.package = std::move(package).value();
    cache.push_back(result);
    return result;
}

} // namespace truffle::shader

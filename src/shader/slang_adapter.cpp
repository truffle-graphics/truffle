#include "truffle/shader/compiler.hpp"
#include "digest.hpp"
#include "compiler_inputs.hpp"
#include "spirv_reflection.hpp"
#include "spirv_translation.hpp"

#include <slang.h>
#include <slang-com-ptr.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

namespace truffle::shader {
namespace {

using Slang::ComPtr;
using detail::field;

rhi::Status invalid(std::string message) {
    return rhi::Status::failure(rhi::StatusCode::invalid_argument, std::move(message));
}

bool same(const SlangUUID& a, const SlangUUID& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

std::string source_path(const CompileRequest& request) {
    // Slang's module API infers GLSL from a .vert/.frag/etc. path even when
    // loading explicit source text. Preserve the caller's identity separately.
    return request.source.identity.ends_with(".slang")
        ? request.source.identity : request.source.identity + ".slang";
}

class Blob final : public ISlangBlob {
public:
    explicit Blob(std::string text) : text_(std::move(text)) {}
    SlangResult SLANG_MCALL queryInterface(const SlangUUID& guid, void** out) noexcept override {
        *out = nullptr;
        if (!same(guid, ISlangUnknown::getTypeGuid()) && !same(guid, ISlangBlob::getTypeGuid()))
            return SLANG_E_NO_INTERFACE;
        *out = static_cast<ISlangBlob*>(this);
        addRef();
        return SLANG_OK;
    }
    uint32_t SLANG_MCALL addRef() noexcept override { return ++refs_; }
    uint32_t SLANG_MCALL release() noexcept override {
        const auto count = --refs_;
        if (!count) delete this;
        return count;
    }
    const void* SLANG_MCALL getBufferPointer() noexcept override { return text_.c_str(); }
    size_t SLANG_MCALL getBufferSize() noexcept override { return text_.size(); }

private:
    std::atomic<uint32_t> refs_{1};
    std::string text_;
};

// Slang owns import spelling, search order, cycle detection and composition.
// This filesystem only supplies an immutable, identity-checked source snapshot.
class FileSystem final : public ISlangFileSystemExt {
public:
    explicit FileSystem(const CompileRequest& request) : request_(request) {
        sources.emplace(request.source.identity, request.source.text);
        snapshots_.emplace(request.source.identity, request.source);
        snapshots_.emplace(source_path(request), request.source);
    }
    SlangResult SLANG_MCALL queryInterface(const SlangUUID& guid, void** out) noexcept override {
        *out = castAs(guid);
        if (!*out) return SLANG_E_NO_INTERFACE;
        addRef();
        return SLANG_OK;
    }
    void* SLANG_MCALL castAs(const SlangUUID& guid) noexcept override {
        if (same(guid, ISlangUnknown::getTypeGuid()) || same(guid, ISlangCastable::getTypeGuid()) ||
            same(guid, ISlangFileSystem::getTypeGuid()) || same(guid, ISlangFileSystemExt::getTypeGuid()))
            return static_cast<ISlangFileSystemExt*>(this);
        return nullptr;
    }
    uint32_t SLANG_MCALL addRef() noexcept override { return ++refs_; }
    uint32_t SLANG_MCALL release() noexcept override {
        const auto count = --refs_;
        if (!count) delete this;
        return count;
    }
    SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** out) noexcept override {
        *out = nullptr;
        const auto* source = load(path);
        if (!source) return SLANG_E_NOT_FOUND;
        *out = new Blob{source->text};
        return SLANG_OK;
    }
    SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path, ISlangBlob** out) noexcept override {
        *out = nullptr;
        const auto* source = load(path);
        if (!source) return SLANG_E_NOT_FOUND;
        *out = new Blob{source->identity};
        return SLANG_OK;
    }
    SlangResult SLANG_MCALL calcCombinedPath(SlangPathType type, const char* from,
                                            const char* path, ISlangBlob** out) noexcept override {
        auto base = std::filesystem::path{from};
        if (type == SLANG_PATH_TYPE_FILE) base = base.parent_path();
        auto combined = (base / path).lexically_normal().generic_string();
        const auto parent = snapshots_.find(from);
        origins_.insert_or_assign(combined, std::pair{std::string{path},
            parent == snapshots_.end() ? std::string{from} : parent->second.identity});
        *out = new Blob{std::move(combined)};
        return SLANG_OK;
    }
    SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* out) noexcept override {
        if (!load(path)) return SLANG_E_NOT_FOUND;
        *out = SLANG_PATH_TYPE_FILE;
        return SLANG_OK;
    }
    SlangResult SLANG_MCALL getPath(PathKind kind, const char* path, ISlangBlob** out) noexcept override {
        *out = nullptr;
        if (kind == PathKind::OperatingSystem) return SLANG_E_NOT_IMPLEMENTED;
        if (kind == PathKind::Canonical) return getFileUniqueIdentity(path, out);
        *out = new Blob{std::filesystem::path{path}.lexically_normal().generic_string()};
        return SLANG_OK;
    }
    void SLANG_MCALL clearCache() noexcept override {}
    SlangResult SLANG_MCALL enumeratePathContents(const char*, FileSystemContentsCallBack,
                                                 void*) noexcept override {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    OSPathKind SLANG_MCALL getOSPathKind() noexcept override { return OSPathKind::None; }

    std::map<std::string, std::string> sources;
    std::string error;

private:
    const Source* load(const std::string& path) {
        if (const auto it = snapshots_.find(path); it != snapshots_.end()) return &it->second;
        // Serialized module binaries can hide their original import closure.
        if (std::filesystem::path{path}.extension() == ".slang-module") return nullptr;
        auto loaded = read(path);
        if (!loaded.ok()) {
            error = loaded.status().message;
            return nullptr;
        }
        auto source = std::move(loaded).value();
        if (source.identity.empty() || source.identity.find('\0') != std::string::npos ||
            source.text.find('\0') != std::string::npos) {
            error = "Invalid Slang source identity/content: " + path;
            return nullptr;
        }
        const auto [it, inserted] = sources.emplace(source.identity, source.text);
        if (!inserted && it->second != source.text) {
            error = "Slang source identity has conflicting contents: " + source.identity;
            return nullptr;
        }
        snapshots_.emplace(source.identity, source);
        return &snapshots_.emplace(path, std::move(source)).first->second;
    }
    rhi::Result<Source> read(const std::string& path) {
        if (!request_.resolver) return read_source(path);
        const auto origin = origins_.find(path);
        return origin == origins_.end()
            ? request_.resolver(path, request_.source.identity, false)
            : request_.resolver(origin->second.first, origin->second.second, false);
    }

    const CompileRequest& request_;
    std::atomic<uint32_t> refs_{1};
    std::map<std::string, Source> snapshots_;
    std::map<std::string, std::pair<std::string, std::string>> origins_;
};

bool identifier(std::string_view value) {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

std::string diagnostics(ISlangBlob* blob) {
    if (!blob) return {};
    return {static_cast<const char*>(blob->getBufferPointer()), std::min(blob->getBufferSize(), size_t{16384})};
}

struct Process {
    std::mutex mutex;
    ComPtr<::slang::IGlobalSession> global;
    Process() { ::slang::createGlobalSession(global.writeRef()); }
};

} // namespace

rhi::Result<Compilation> compile_slang(const CompileRequest& request, std::vector<Compilation>& cache) {
    if (request.name.empty() || request.name.find('\0') != std::string::npos ||
        request.source.identity.empty() || request.source.identity.find('\0') != std::string::npos ||
        request.source.text.empty() ||
        request.source.text.find('\0') != std::string::npos || !identifier(request.entryPoint))
        return invalid("Slang requires source identity/content/name and a valid entry point");
    auto definesResult = detail::normalized_defines(request.defines);
    if (!definesResult.ok()) return definesResult.status();
    const auto defines = std::move(definesResult).value();
    std::string configuration;
    std::vector<::slang::PreprocessorMacroDesc> macros;
    for (const auto& define : defines) {
        macros.push_back({define.name.c_str(), define.value.c_str()});
        field(configuration, define.name);
        field(configuration, define.value);
    }
    SlangStage stage;
    switch (request.stage) {
    case rhi::ShaderStage::vertex: stage = SLANG_STAGE_VERTEX; break;
    case rhi::ShaderStage::fragment: stage = SLANG_STAGE_FRAGMENT; break;
    case rhi::ShaderStage::compute: stage = SLANG_STAGE_COMPUTE; break;
    default: return invalid("Unsupported Slang stage");
    }
    static Process process;
    const std::lock_guard lock{process.mutex};
    if (!process.global || std::string_view{process.global->getBuildTagString()} != "2026.12.1")
        return rhi::Status::failure(rhi::StatusCode::unsupported, "Slang requires audited compiler 2026.12.1");

    ComPtr<FileSystem> files;
    files.attach(new FileSystem{request});
    std::vector<const char*> roots;
    for (const auto& root : request.includeRoots) {
        if (root.find('\0') != std::string::npos) return invalid("Invalid Slang search root");
        roots.push_back(root.c_str());
    }
    ::slang::CompilerOptionEntry options[5]{};
    options[0].name = ::slang::CompilerOptionName::EmitSpirvDirectly;
    options[0].value.intValue0 = 1;
    options[1].name = ::slang::CompilerOptionName::VulkanUseEntryPointName;
    options[1].value.intValue0 = 1;
    options[2].name = ::slang::CompilerOptionName::DebugInformation;
    options[2].value.intValue0 = request.generateDebugInfo ? SLANG_DEBUG_INFO_LEVEL_STANDARD : SLANG_DEBUG_INFO_LEVEL_NONE;
    options[3].name = ::slang::CompilerOptionName::Optimization;
    options[3].value.intValue0 = SLANG_OPTIMIZATION_LEVEL_NONE;
    options[4].name = ::slang::CompilerOptionName::SkipDownstreamLinking;
    options[4].value.intValue0 = 1;
    ::slang::TargetDesc target;
    target.format = SLANG_SPIRV;
    target.profile = process.global->findProfile("spirv_1_3");
    target.compilerOptionEntries = options;
    target.compilerOptionEntryCount = 5;
    ::slang::SessionDesc desc;
    desc.targets = &target;
    desc.targetCount = 1;
    desc.searchPaths = roots.data();
    desc.searchPathCount = static_cast<SlangInt>(roots.size());
    desc.preprocessorMacros = macros.data();
    desc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
    desc.fileSystem = files;
    // Validation is performed below with the independently pinned validator and
    // explicit Vulkan environment, never by loading an optional Slang plugin.
    desc.skipSPIRVValidation = true;
    ComPtr<::slang::ISession> session;
    if (SLANG_FAILED(process.global->createSession(desc, session.writeRef())))
        return invalid("Slang session creation failed");
    ComPtr<ISlangBlob> log;
    const auto path = source_path(request);
    auto* module = session->loadModuleFromSourceString("truffle_root", path.c_str(),
                                                      request.source.text.c_str(), log.writeRef());
    std::string messages = diagnostics(log);
    if (!module) return invalid("Slang module/import compilation failed:\n" + messages + "\n" + files->error);
    ComPtr<::slang::IEntryPoint> entry;
    log.setNull();
    if (SLANG_FAILED(module->findAndCheckEntryPoint(request.entryPoint.c_str(), stage,
                                                   entry.writeRef(), log.writeRef())))
        return invalid("Slang entry point failed:\n" + diagnostics(log));
    messages += diagnostics(log);
    ::slang::IComponentType* components[]{module, entry};
    ComPtr<::slang::IComponentType> composed, linked;
    log.setNull();
    if (SLANG_FAILED(session->createCompositeComponentType(components, 2, composed.writeRef(), log.writeRef())))
        return invalid("Slang component composition failed:\n" + diagnostics(log));
    messages += diagnostics(log);
    log.setNull();
    if (SLANG_FAILED(composed->link(linked.writeRef(), log.writeRef())))
        return invalid("Slang component linking failed:\n" + diagnostics(log));
    messages += diagnostics(log);
    auto* layout = linked->getLayout();
    if (!layout || layout->getEntryPointCount() != 1 || layout->getEntryPointByIndex(0)->getStage() != stage)
        return invalid("Slang requested entry point/stage does not match linked program");

    Compilation result;
    result.vulkanTarget = request.vulkanTarget;
    result.compiler = {"slang", "2026.12.1", TRUFFLE_SLANG_REVISION
        ";truffle-slang=1;provider=" TRUFFLE_SLANG_PROVIDER
        ";spirv=1.3;vulkan=1.1;direct;row-major;optimization=none;entry-name;native-link-only;root-slang-alias;"
        "spirv-tools=" TRUFFLE_SPIRV_TOOLS_REVISION ";spirv-headers=" TRUFFLE_SPIRV_HEADERS_REVISION};
    result.compiler.revision += std::string{";debug="} + (request.generateDebugInfo ? "1" : "0");
    std::string key;
    field(key, result.compiler.revision);
    field(key, request.name);
    field(key, request.source.identity);
    field(key, request.entryPoint);
    field(key, std::to_string(static_cast<int>(request.stage)));
    field(key, configuration);
    field(key, std::to_string(request.includeRoots.size()));
    for (const auto& root : request.includeRoots) field(key, root);
    field(key, std::to_string(files->sources.size()));
    for (const auto& [identity, content] : files->sources) {
        const auto hash = detail::sha256(content);
        result.dependencies.push_back({identity, hash});
        field(key, identity);
        field(key, hash);
    }
    result.cacheKey = detail::sha256(key);
    for (const auto& previous : cache) {
        if (previous.cacheKey == result.cacheKey) {
            result = previous;
            result.cacheHit = true;
            return result;
        }
    }
    ComPtr<ISlangBlob> code;
    log.setNull();
    if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), log.writeRef())) || !code)
        return invalid("Slang SPIR-V emission failed:\n" + diagnostics(log));
    messages += diagnostics(log);
    if (code->getBufferSize() % sizeof(unsigned int) != 0)
        return invalid("Slang returned malformed SPIR-V bytes");
    std::vector<unsigned int> words(code->getBufferSize() / sizeof(unsigned int));
    std::memcpy(words.data(), code->getBufferPointer(), code->getBufferSize());
    auto status = detail::validate_spirv(words, request.vulkanTarget);
    if (!status.ok()) return status;
    rhi::ShaderVariantDesc variant;
    variant.target = request.target;
    variant.format = rhi::ShaderByteFormat::spirv;
    variant.kind = rhi::ShaderVariantKind::generated;
    variant.stage = request.stage;
    variant.entryPoint = request.entryPoint;
    status = detail::reflect_spirv(words, request.stage, variant.reflection);
    if (!status.ok()) return status;
    variant.code.resize(code->getBufferSize());
    std::memcpy(variant.code.data(), code->getBufferPointer(), variant.code.size());
    result.diagnostics = messages.substr(0, 16384);
    rhi::ShaderPackageDesc packageDesc;
    packageDesc.name = request.name;
    packageDesc.compilers.push_back(result.compiler);
    for (const auto& dependency : result.dependencies)
        packageDesc.sources.push_back({dependency.identity, request.language, dependency.sha256});
    if (!defines.empty()) {
        variant.permutation = detail::sha256(configuration);
        packageDesc.permutations.push_back({variant.permutation, defines, {}});
    }
    if (!result.diagnostics.empty())
        packageDesc.diagnostics.push_back({rhi::ShaderDiagnosticSeverity::info,
            "spirv:" + std::to_string(static_cast<int>(request.stage)) + ":" +
                request.entryPoint + ":" + variant.permutation, result.diagnostics.substr(0, 4096)});
    packageDesc.variants.push_back(std::move(variant));
    auto package = rhi::ShaderPackage::create(std::move(packageDesc));
    if (!package.ok()) return package.status();
    result.package = std::move(package).value();
    cache.push_back(result);
    return result;
}

} // namespace truffle::shader

#pragma once

#include "truffle/rhi/shader_package.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace truffle::shader {

struct Source {
    std::string identity;
    std::string text;
};

// Return stable, unique identities; the callback is borrowed only during compile().
using SourceResolver = std::function<rhi::Result<Source>(
    std::string_view requested, std::string_view including, bool system)>;

enum class VulkanVersion { v1_0, v1_1 };
enum class SpirvVersion { v1_0, v1_3 };
enum class MetalVersion { v2_1 };
enum class GlslVersion { v410 };
enum class GlslEsVersion { v300 };

struct MetalTarget {
    MetalVersion version = MetalVersion::v2_1;
    bool operator==(const MetalTarget&) const = default;
};
struct GlslTarget {
    GlslVersion version = GlslVersion::v410;
    bool operator==(const GlslTarget&) const = default;
};
struct GlslEsTarget {
    GlslEsVersion version = GlslEsVersion::v300;
    bool operator==(const GlslEsTarget&) const = default;
};

struct VulkanTarget {
    VulkanVersion environment = VulkanVersion::v1_1;
    SpirvVersion spirv = SpirvVersion::v1_3;

    bool operator==(const VulkanTarget&) const = default;
};

// Each listed language/target supports every listed stage and configuration,
// subject to source/reflection limits. Spans refer to process-lifetime data.
struct CompilerRoute {
    rhi::ShaderSourceLanguage language;
    rhi::ShaderTarget target;
    std::span<const rhi::ShaderStage> stages;
    std::span<const VulkanTarget> vulkanTargets;
    std::span<const MetalTarget> metalTargets{};
    std::span<const GlslTarget> glslTargets{};
    std::span<const GlslEsTarget> glslEsTargets{};
    bool experimental = false;
};

// Unknown enum values have no name.
[[nodiscard]] std::string_view vulkan_version_name(VulkanVersion version) noexcept;
[[nodiscard]] std::string_view spirv_version_name(SpirvVersion version) noexcept;

struct CompileRequest {
    std::string name = "shader";
    Source source;
    rhi::ShaderStage stage = rhi::ShaderStage::vertex;
    std::string entryPoint = "main";
    rhi::ShaderSourceLanguage language = rhi::ShaderSourceLanguage::glsl;
    rhi::ShaderTarget target = rhi::ShaderTarget::spirv;
    VulkanTarget vulkanTarget;
    MetalTarget metalTarget;
    GlslTarget glslTarget;
    GlslEsTarget glslEsTarget;
    // Explicit consent is required both to produce and to load generated MSL.
    bool allowExperimentalMetalGeneration = false;
    std::vector<std::string> includeRoots;
    SourceResolver resolver;
    std::vector<rhi::ShaderDefine> defines;
    bool generateDebugInfo = false;
};

struct Dependency {
    std::string identity;
    std::string sha256;
};

struct Compilation {
    rhi::ShaderPackage package;
    std::vector<Dependency> dependencies;
    rhi::ShaderCompilerRecord compiler;
    VulkanTarget vulkanTarget;
    std::string cacheKey;
    std::string diagnostics;
    bool cacheHit = false;

    // Borrow the package's canonical full directional IO; no second copy.
    [[nodiscard]] std::span<const rhi::ShaderInterfaceVariable> interfaces() const noexcept {
        const auto& variants = package.desc().variants;
        return variants.empty() ? std::span<const rhi::ShaderInterfaceVariable>{}
                                : variants.front().reflection.interfaces;
    }
};

// Instances are single-thread-owned. Distinct instances may compile concurrently.
// Results own all data and outlive the compiler. Failed attempts never enter cache.
class Compiler {
public:
    Compiler();
    ~Compiler();
    Compiler(const Compiler&) = delete;
    Compiler& operator=(const Compiler&) = delete;

    [[nodiscard]] rhi::Result<Compilation> compile(const CompileRequest& request);
    void clear_cache();
    [[nodiscard]] static std::span<const CompilerRoute> routes() noexcept;
    // Whether any source-compilation route is built in; not a native GPU probe.
    [[nodiscard]] static bool available() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Poll by supplying the current request/root source. Includes are re-resolved on
// every attempt; last_good() is independent of the latest attempt's status.
class RebuildState {
public:
    [[nodiscard]] rhi::Result<Compilation> rebuild(
        Compiler& compiler, const CompileRequest& request);
    [[nodiscard]] const Compilation* last_good() const noexcept;

private:
    std::unique_ptr<Compilation> lastGood_;
};

[[nodiscard]] rhi::Result<Source> read_source(std::string_view path);

} // namespace truffle::shader

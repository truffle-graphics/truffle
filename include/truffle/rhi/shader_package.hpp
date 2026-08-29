#pragma once

#include "truffle/rhi/status.hpp"
#include "truffle/rhi/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace truffle::rhi {

inline constexpr std::uint16_t shader_package_schema_major = 1;
inline constexpr std::uint16_t shader_package_schema_minor = 0;

enum class ShaderTarget : std::uint8_t {
    spirv,
    dxil,
    metal_library,
    metal_source,
    wgsl,
    glsl,
    glsl_es,
};

enum class ShaderVariantKind : std::uint8_t {
    generated,
    precompiled,
    native_override,
};

enum class ShaderSourceLanguage : std::uint8_t {
    slang,
    hlsl,
    glsl,
    glsl_es,
    wgsl,
    msl,
    spirv,
    dxil,
    metal_library,
    backend_native,
};

enum class ShaderValueType : std::uint8_t {
    boolean,
    sint32,
    uint32,
    float32,
};

enum class ShaderDiagnosticSeverity : std::uint8_t {
    info,
    warning,
    error,
};

struct ShaderDefine {
    std::string name;
    std::string value;

    bool operator==(const ShaderDefine&) const = default;
};

struct ShaderSpecializationConstant {
    std::uint32_t id = 0;
    std::string name;
    ShaderValueType type = ShaderValueType::uint32;
    std::uint32_t defaultValueBits = 0;

    bool operator==(const ShaderSpecializationConstant&) const = default;
};

struct ShaderPermutation {
    std::string name;
    std::vector<ShaderDefine> defines;
    std::vector<ShaderSpecializationConstant> specializationConstants;

    bool operator==(const ShaderPermutation&) const = default;
};

struct PushConstantRange {
    ShaderStage stage = ShaderStage::vertex;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;

    bool operator==(const PushConstantRange&) const = default;
};

struct ShaderInterfaceVariable {
    ShaderStage stage = ShaderStage::vertex;
    std::uint32_t location = 0;
    std::string name;
    std::string type;
    std::string interpolation;

    bool operator==(const ShaderInterfaceVariable&) const = default;
};

struct ShaderReflection {
    std::vector<ResourceBinding> bindings;
    std::vector<PushConstantRange> pushConstants;
    std::vector<ShaderSpecializationConstant> specializationConstants;
    std::vector<ShaderInterfaceVariable> interfaces;
    Extent3D requiredWorkgroupSize{1, 1, 1};
    Extent3D preferredWorkgroupSize{1, 1, 1};
};

struct ShaderBindingRemap {
    ShaderTarget target = ShaderTarget::spirv;
    ShaderStage stage = ShaderStage::vertex;
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::uint32_t arrayElement = 0;
    std::uint32_t nativeGroup = 0;
    std::uint32_t nativeBinding = 0;
    std::uint32_t nativeArrayElement = 0;

    bool operator==(const ShaderBindingRemap&) const = default;
};

struct ShaderSourceRecord {
    std::string path;
    ShaderSourceLanguage language = ShaderSourceLanguage::backend_native;
    std::string sha256;

    bool operator==(const ShaderSourceRecord&) const = default;
};

struct ShaderCompilerRecord {
    std::string name;
    std::string version;
    std::string revision;

    bool operator==(const ShaderCompilerRecord&) const = default;
};

struct ShaderPackageDiagnostic {
    ShaderDiagnosticSeverity severity = ShaderDiagnosticSeverity::info;
    std::string variant;
    std::string message;

    bool operator==(const ShaderPackageDiagnostic&) const = default;
};

struct ShaderVariantDesc {
    ShaderTarget target = ShaderTarget::spirv;
    ShaderByteFormat format = ShaderByteFormat::spirv;
    ShaderVariantKind kind = ShaderVariantKind::precompiled;
    ShaderStage stage = ShaderStage::vertex;
    std::string entryPoint = "main";
    std::string permutation;
    ShaderReflection reflection;
    std::vector<std::byte> code;
};

struct ShaderPackageDesc {
    std::uint16_t schemaMajor = shader_package_schema_major;
    std::uint16_t schemaMinor = shader_package_schema_minor;
    std::string name;
    std::vector<Feature> requiredFeatures;
    std::vector<ShaderPermutation> permutations;
    std::vector<ShaderBindingRemap> remaps;
    std::vector<ShaderSourceRecord> sources;
    std::vector<ShaderCompilerRecord> compilers;
    std::vector<ShaderPackageDiagnostic> diagnostics;
    std::vector<ShaderVariantDesc> variants;
};

struct ShaderPackageOptions {
    std::vector<Feature> availableFeatures;
    bool enforceCapabilities = false;
    bool allowExperimentalMetalGeneration = false;
    bool allowExperimentalWgslGeneration = false;
};

class ShaderPackage {
public:
    ShaderPackage() = default;

    [[nodiscard]] static Result<ShaderPackage> create(
        ShaderPackageDesc desc, const ShaderPackageOptions& options = {});
    [[nodiscard]] static Result<ShaderPackage> load(
        std::span<const std::byte> bytes,
        const ShaderPackageOptions& options = {});

    [[nodiscard]] const ShaderPackageDesc& desc() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] const ShaderVariantDesc* find_variant(
        ShaderTarget target, std::string_view entryPoint, ShaderStage stage,
        std::string_view permutation = {}) const noexcept;
    [[nodiscard]] Result<const ShaderVariantDesc*> select_variant(
        ShaderTarget target, std::string_view entryPoint, ShaderStage stage,
        std::string_view permutation = {}) const;

private:
    ShaderPackage(ShaderPackageDesc desc, std::vector<std::byte> bytes);

    ShaderPackageDesc desc_;
    std::vector<std::byte> bytes_;
};

[[nodiscard]] std::string_view shader_target_name(ShaderTarget target) noexcept;
[[nodiscard]] Result<ShaderTarget> shader_target_from_name(
    std::string_view name);
[[nodiscard]] std::string_view shader_source_language_name(
    ShaderSourceLanguage language) noexcept;
[[nodiscard]] Result<ShaderSourceLanguage> shader_source_language_from_name(
    std::string_view name);

} // namespace truffle::rhi

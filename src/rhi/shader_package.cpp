#include "truffle/rhi/shader_package.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace truffle::rhi {
namespace {

constexpr std::array<std::byte, 8> packageMagic{
    std::byte{'T'}, std::byte{'R'}, std::byte{'F'}, std::byte{'S'},
    std::byte{'H'}, std::byte{'P'}, std::byte{'K'}, std::byte{'G'}};
constexpr std::uint16_t containerVersion = 1;
constexpr std::size_t maximumPackageBytes = 256u * 1024u * 1024u;
constexpr std::size_t maximumStringBytes = 1024u * 1024u;
constexpr std::uint32_t maximumRecords = 65535;

[[nodiscard]] Status invalid(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::uint64_t hash_bytes(
    std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

class Writer {
public:
    void u8(std::uint8_t value) { data_.push_back(std::byte{value}); }

    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8u));
    }

    void u32(std::uint32_t value) {
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void u64(std::uint64_t value) {
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void string(std::string_view value) {
        if (value.size() > maximumStringBytes) {
            throw std::length_error{"shader package string exceeds limit"};
        }
        u32(static_cast<std::uint32_t>(value.size()));
        append(std::as_bytes(std::span{value.data(), value.size()}));
    }

    void append(std::span<const std::byte> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] const std::vector<std::byte>& data() const noexcept {
        return data_;
    }

    [[nodiscard]] std::vector<std::byte> take() { return std::move(data_); }

private:
    std::vector<std::byte> data_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool u8(std::uint8_t& value) {
        if (remaining() < 1) {
            return false;
        }
        value = std::to_integer<std::uint8_t>(bytes_[position_++]);
        return true;
    }

    [[nodiscard]] bool u16(std::uint16_t& value) {
        value = 0;
        for (std::uint32_t shift = 0; shift < 16; shift += 8) {
            std::uint8_t part = 0;
            if (!u8(part)) {
                return false;
            }
            value |= static_cast<std::uint16_t>(part) << shift;
        }
        return true;
    }

    [[nodiscard]] bool u32(std::uint32_t& value) {
        value = 0;
        for (std::uint32_t shift = 0; shift < 32; shift += 8) {
            std::uint8_t part = 0;
            if (!u8(part)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(part) << shift;
        }
        return true;
    }

    [[nodiscard]] bool u64(std::uint64_t& value) {
        value = 0;
        for (std::uint32_t shift = 0; shift < 64; shift += 8) {
            std::uint8_t part = 0;
            if (!u8(part)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(part) << shift;
        }
        return true;
    }

    [[nodiscard]] bool string(std::string& value) {
        std::uint32_t size = 0;
        if (!u32(size) || size > maximumStringBytes || remaining() < size) {
            return false;
        }
        const auto* first = reinterpret_cast<const char*>(
            bytes_.data() + static_cast<std::ptrdiff_t>(position_));
        value.assign(first, size);
        position_ += size;
        return true;
    }

    [[nodiscard]] bool slice(std::size_t size,
                             std::span<const std::byte>& value) {
        if (remaining() < size) {
            return false;
        }
        value = bytes_.subspan(position_, size);
        position_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

template <typename T>
void write_enum(Writer& writer, T value) {
    writer.u8(static_cast<std::uint8_t>(value));
}

template <typename T>
[[nodiscard]] bool read_enum(Reader& reader, T& value, T last) {
    std::uint8_t encoded = 0;
    if (!reader.u8(encoded) || encoded > static_cast<std::uint8_t>(last)) {
        return false;
    }
    value = static_cast<T>(encoded);
    return true;
}

template <typename T, typename Write>
void write_vector(Writer& writer, const std::vector<T>& values, Write write) {
    if (values.size() > maximumRecords) {
        throw std::length_error{"shader package vector exceeds limit"};
    }
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        write(writer, value);
    }
}

template <typename T, typename Read>
[[nodiscard]] bool read_vector(Reader& reader, std::vector<T>& values,
                               Read read) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || count > maximumRecords) {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        T value;
        if (!read(reader, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

[[nodiscard]] auto binding_key(const ResourceBinding& value) {
    return std::tuple{value.group, value.binding,
                      static_cast<std::uint8_t>(value.stage), value.name,
                      static_cast<std::uint8_t>(value.type), value.arrayCount,
                      value.minimumSize, value.readOnly};
}

[[nodiscard]] auto interface_key(const ShaderInterfaceVariable& value) {
    return std::tuple{static_cast<std::uint8_t>(value.stage), value.location,
                      value.name, value.type, value.interpolation};
}

[[nodiscard]] bool extent_equal(Extent3D lhs, Extent3D rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.depth == rhs.depth;
}

[[nodiscard]] bool binding_equal(const ResourceBinding& lhs,
                                 const ResourceBinding& rhs) noexcept {
    return lhs.name == rhs.name && lhs.stage == rhs.stage &&
           lhs.type == rhs.type && lhs.group == rhs.group &&
           lhs.binding == rhs.binding && lhs.arrayCount == rhs.arrayCount &&
           lhs.minimumSize == rhs.minimumSize && lhs.readOnly == rhs.readOnly;
}

[[nodiscard]] bool reflection_equal(const ShaderReflection& lhs,
                                    const ShaderReflection& rhs) noexcept {
    if (lhs.bindings.size() != rhs.bindings.size() ||
        lhs.pushConstants != rhs.pushConstants ||
        lhs.specializationConstants != rhs.specializationConstants ||
        lhs.interfaces != rhs.interfaces ||
        !extent_equal(lhs.requiredWorkgroupSize, rhs.requiredWorkgroupSize) ||
        !extent_equal(lhs.preferredWorkgroupSize,
                      rhs.preferredWorkgroupSize)) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.bindings.size(); ++index) {
        if (!binding_equal(lhs.bindings[index], rhs.bindings[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Status normalize_reflection(ShaderReflection& reflection) {
    std::sort(reflection.bindings.begin(), reflection.bindings.end(),
              [](const auto& lhs, const auto& rhs) {
                  return binding_key(lhs) < binding_key(rhs);
              });
    for (std::size_t index = 0; index < reflection.bindings.size(); ++index) {
        const auto& binding = reflection.bindings[index];
        if (binding.arrayCount == 0) {
            return invalid("shader binding arrays must be non-empty");
        }
        if (index != 0) {
            const auto& previous = reflection.bindings[index - 1];
            if (binding.group == previous.group &&
                binding.binding == previous.binding &&
                binding.stage == previous.stage) {
                return invalid("shader reflection has duplicate logical bindings");
            }
        }
    }

    std::sort(reflection.pushConstants.begin(), reflection.pushConstants.end(),
              [](const auto& lhs, const auto& rhs) {
                  return std::tuple{static_cast<std::uint8_t>(lhs.stage),
                                    lhs.offset, lhs.size} <
                         std::tuple{static_cast<std::uint8_t>(rhs.stage),
                                    rhs.offset, rhs.size};
              });
    for (const auto& range : reflection.pushConstants) {
        if (range.size == 0) {
            return invalid("push-constant ranges must be non-empty");
        }
    }
    for (std::size_t index = 1; index < reflection.pushConstants.size();
         ++index) {
        const auto& previous = reflection.pushConstants[index - 1];
        const auto& current = reflection.pushConstants[index];
        if (previous.stage == current.stage &&
            static_cast<std::uint64_t>(current.offset) <
                static_cast<std::uint64_t>(previous.offset) + previous.size) {
            return invalid("push-constant ranges must not overlap per stage");
        }
    }

    std::sort(reflection.specializationConstants.begin(),
              reflection.specializationConstants.end(),
              [](const auto& lhs, const auto& rhs) {
                  return std::tuple{lhs.id, lhs.name} <
                         std::tuple{rhs.id, rhs.name};
              });
    for (std::size_t index = 1;
         index < reflection.specializationConstants.size(); ++index) {
        if (reflection.specializationConstants[index - 1].id ==
            reflection.specializationConstants[index].id) {
            return invalid(
                "shader reflection has duplicate specialization constants");
        }
    }

    std::sort(reflection.interfaces.begin(), reflection.interfaces.end(),
              [](const auto& lhs, const auto& rhs) {
                  return interface_key(lhs) < interface_key(rhs);
              });
    for (std::size_t index = 1; index < reflection.interfaces.size(); ++index) {
        const auto& previous = reflection.interfaces[index - 1];
        const auto& current = reflection.interfaces[index];
        if (previous.stage == current.stage &&
            previous.location == current.location) {
            return invalid("shader reflection has duplicate interface locations");
        }
    }

    if (reflection.requiredWorkgroupSize.width == 0 ||
        reflection.requiredWorkgroupSize.height == 0 ||
        reflection.requiredWorkgroupSize.depth == 0 ||
        reflection.preferredWorkgroupSize.width == 0 ||
        reflection.preferredWorkgroupSize.height == 0 ||
        reflection.preferredWorkgroupSize.depth == 0) {
        return invalid("shader workgroup sizes must be non-zero");
    }
    return Status::success();
}

[[nodiscard]] bool sha256_valid(std::string_view value) noexcept {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0 ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool format_matches_target(const ShaderVariantDesc& variant) {
    switch (variant.target) {
    case ShaderTarget::spirv:
        return variant.format == ShaderByteFormat::spirv;
    case ShaderTarget::dxil:
        return variant.format == ShaderByteFormat::dxil ||
               (variant.format == ShaderByteFormat::native_source &&
                variant.kind == ShaderVariantKind::native_override);
    case ShaderTarget::metal_library:
        return variant.format == ShaderByteFormat::metal_library;
    case ShaderTarget::metal_source:
    case ShaderTarget::wgsl:
    case ShaderTarget::glsl:
    case ShaderTarget::glsl_es:
        return variant.format == ShaderByteFormat::native_source;
    }
    return false;
}

[[nodiscard]] auto variant_key(const ShaderVariantDesc& value) {
    return std::tuple{static_cast<std::uint8_t>(value.target),
                      static_cast<std::uint8_t>(value.stage), value.entryPoint,
                      value.permutation,
                      static_cast<std::uint8_t>(value.kind)};
}

[[nodiscard]] Status normalize(ShaderPackageDesc& desc,
                               const ShaderPackageOptions& options) {
    if (desc.schemaMajor != shader_package_schema_major ||
        desc.schemaMinor > shader_package_schema_minor) {
        return invalid("shader package schema version is incompatible");
    }
    if (desc.name.empty() || desc.sources.empty() || desc.compilers.empty() ||
        desc.variants.empty()) {
        return invalid("shader package requires a name, source hashes, compiler "
                       "identity, and at least one variant");
    }

    std::sort(desc.requiredFeatures.begin(), desc.requiredFeatures.end(),
              [](Feature lhs, Feature rhs) {
                  return static_cast<std::uint8_t>(lhs) <
                         static_cast<std::uint8_t>(rhs);
              });
    desc.requiredFeatures.erase(
        std::unique(desc.requiredFeatures.begin(), desc.requiredFeatures.end()),
        desc.requiredFeatures.end());
    if (options.enforceCapabilities) {
        for (const auto feature : desc.requiredFeatures) {
            if (std::find(options.availableFeatures.begin(),
                          options.availableFeatures.end(), feature) ==
                options.availableFeatures.end()) {
                return Status::failure(
                    StatusCode::unsupported,
                    "shader package requires an unavailable capability");
            }
        }
    }

    for (auto& permutation : desc.permutations) {
        if (permutation.name.empty()) {
            return invalid("shader permutation names must be non-empty");
        }
        std::sort(permutation.defines.begin(), permutation.defines.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return std::tuple{lhs.name, lhs.value} <
                             std::tuple{rhs.name, rhs.value};
                  });
        for (std::size_t index = 1; index < permutation.defines.size(); ++index) {
            if (permutation.defines[index - 1].name ==
                permutation.defines[index].name) {
                return invalid("shader permutation has duplicate defines");
            }
        }
        std::sort(permutation.specializationConstants.begin(),
                  permutation.specializationConstants.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return std::tuple{lhs.id, lhs.name} <
                             std::tuple{rhs.id, rhs.name};
                  });
        for (std::size_t index = 1;
             index < permutation.specializationConstants.size(); ++index) {
            if (permutation.specializationConstants[index - 1].id ==
                permutation.specializationConstants[index].id) {
                return invalid(
                    "shader permutation has duplicate specialization constants");
            }
        }
    }
    std::sort(desc.permutations.begin(), desc.permutations.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.name < rhs.name;
              });
    for (std::size_t index = 1; index < desc.permutations.size(); ++index) {
        if (desc.permutations[index - 1].name == desc.permutations[index].name) {
            return invalid("shader package has duplicate permutations");
        }
    }

    std::sort(desc.remaps.begin(), desc.remaps.end(), [](const auto& lhs,
                                                         const auto& rhs) {
        return std::tuple{static_cast<std::uint8_t>(lhs.target),
                          static_cast<std::uint8_t>(lhs.stage), lhs.group,
                          lhs.binding, lhs.arrayElement, lhs.nativeGroup,
                          lhs.nativeBinding, lhs.nativeArrayElement} <
               std::tuple{static_cast<std::uint8_t>(rhs.target),
                          static_cast<std::uint8_t>(rhs.stage), rhs.group,
                          rhs.binding, rhs.arrayElement, rhs.nativeGroup,
                          rhs.nativeBinding, rhs.nativeArrayElement};
    });
    for (std::size_t index = 1; index < desc.remaps.size(); ++index) {
        const auto& previous = desc.remaps[index - 1];
        const auto& current = desc.remaps[index];
        if (previous.target == current.target && previous.stage == current.stage &&
            previous.group == current.group &&
            previous.binding == current.binding &&
            previous.arrayElement == current.arrayElement) {
            return invalid("shader package has duplicate binding remaps");
        }
    }

    for (auto& source : desc.sources) {
        std::transform(source.sha256.begin(), source.sha256.end(),
                       source.sha256.begin(), [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        if (source.path.empty() || !sha256_valid(source.sha256)) {
            return invalid("shader source records require a path and SHA-256");
        }
    }
    std::sort(desc.sources.begin(), desc.sources.end(), [](const auto& lhs,
                                                           const auto& rhs) {
        return std::tuple{lhs.path, static_cast<std::uint8_t>(lhs.language),
                          lhs.sha256} <
               std::tuple{rhs.path, static_cast<std::uint8_t>(rhs.language),
                          rhs.sha256};
    });
    for (std::size_t index = 1; index < desc.sources.size(); ++index) {
        if (desc.sources[index - 1].path == desc.sources[index].path) {
            return invalid("shader package has duplicate source paths");
        }
    }

    std::sort(desc.compilers.begin(), desc.compilers.end(),
              [](const auto& lhs, const auto& rhs) {
                  return std::tuple{lhs.name, lhs.version, lhs.revision} <
                         std::tuple{rhs.name, rhs.version, rhs.revision};
              });
    desc.compilers.erase(
        std::unique(desc.compilers.begin(), desc.compilers.end()),
        desc.compilers.end());
    for (const auto& compiler : desc.compilers) {
        if (compiler.name.empty() || compiler.version.empty()) {
            return invalid("shader compiler records require name and version");
        }
    }

    std::sort(desc.diagnostics.begin(), desc.diagnostics.end(),
              [](const auto& lhs, const auto& rhs) {
                  return std::tuple{static_cast<std::uint8_t>(lhs.severity),
                                    lhs.variant, lhs.message} <
                         std::tuple{static_cast<std::uint8_t>(rhs.severity),
                                    rhs.variant, rhs.message};
              });
    desc.diagnostics.erase(
        std::unique(desc.diagnostics.begin(), desc.diagnostics.end()),
        desc.diagnostics.end());
    for (const auto& diagnostic : desc.diagnostics) {
        if (diagnostic.message.size() > 4096) {
            return invalid("shader diagnostics are limited to 4096 bytes");
        }
    }

    for (auto& variant : desc.variants) {
        if (variant.entryPoint.empty() || variant.code.empty() ||
            !format_matches_target(variant)) {
            return invalid("shader variant descriptor is invalid");
        }
        if (!variant.permutation.empty() &&
            std::none_of(desc.permutations.begin(), desc.permutations.end(),
                         [&](const auto& permutation) {
                             return permutation.name == variant.permutation;
                         })) {
            return invalid("shader variant references an unknown permutation");
        }
        if (variant.kind == ShaderVariantKind::generated &&
            (variant.target == ShaderTarget::metal_source ||
             variant.target == ShaderTarget::metal_library) &&
            !options.allowExperimentalMetalGeneration) {
            return Status::failure(
                StatusCode::unsupported,
                "generated Metal shader variants are experimentally gated");
        }
        if (variant.kind == ShaderVariantKind::generated &&
            variant.target == ShaderTarget::wgsl &&
            !options.allowExperimentalWgslGeneration) {
            return Status::failure(
                StatusCode::unsupported,
                "generated WGSL shader variants are experimentally gated");
        }
        const auto status = normalize_reflection(variant.reflection);
        if (!status.ok()) {
            return status;
        }
    }
    std::sort(desc.variants.begin(), desc.variants.end(),
              [](const auto& lhs, const auto& rhs) {
                  return variant_key(lhs) < variant_key(rhs);
              });
    for (std::size_t index = 1; index < desc.variants.size(); ++index) {
        if (variant_key(desc.variants[index - 1]) ==
            variant_key(desc.variants[index])) {
            return invalid("shader package has duplicate target variants");
        }
    }

    for (std::size_t first = 0; first < desc.variants.size(); ++first) {
        for (std::size_t second = first + 1; second < desc.variants.size();
             ++second) {
            const auto& lhs = desc.variants[first];
            const auto& rhs = desc.variants[second];
            if (lhs.stage == rhs.stage && lhs.entryPoint == rhs.entryPoint &&
                lhs.permutation == rhs.permutation &&
                !reflection_equal(lhs.reflection, rhs.reflection)) {
                return invalid(
                    "shader target variants disagree on normalized reflection");
            }
        }
    }
    return Status::success();
}

void write_binding(Writer& writer, const ResourceBinding& value) {
    writer.string(value.name);
    write_enum(writer, value.stage);
    write_enum(writer, value.type);
    writer.u32(value.group);
    writer.u32(value.binding);
    writer.u32(value.arrayCount);
    writer.u64(static_cast<std::uint64_t>(value.minimumSize));
    writer.u8(value.readOnly ? 1 : 0);
}

[[nodiscard]] bool read_binding(Reader& reader, ResourceBinding& value) {
    std::uint64_t minimumSize = 0;
    std::uint8_t readOnly = 0;
    if (!reader.string(value.name) ||
        !read_enum(reader, value.stage, ShaderStage::compute) ||
        !read_enum(reader, value.type, ResourceBindingType::sampler) ||
        !reader.u32(value.group) || !reader.u32(value.binding) ||
        !reader.u32(value.arrayCount) || !reader.u64(minimumSize) ||
        minimumSize > std::numeric_limits<std::size_t>::max() ||
        !reader.u8(readOnly) || readOnly > 1) {
        return false;
    }
    value.minimumSize = static_cast<std::size_t>(minimumSize);
    value.readOnly = readOnly != 0;
    return true;
}

void write_specialization(Writer& writer,
                          const ShaderSpecializationConstant& value) {
    writer.u32(value.id);
    writer.string(value.name);
    write_enum(writer, value.type);
    writer.u32(value.defaultValueBits);
}

[[nodiscard]] bool read_specialization(
    Reader& reader, ShaderSpecializationConstant& value) {
    return reader.u32(value.id) && reader.string(value.name) &&
           read_enum(reader, value.type, ShaderValueType::float32) &&
           reader.u32(value.defaultValueBits);
}

void write_reflection(Writer& writer, const ShaderReflection& value) {
    write_vector(writer, value.bindings, write_binding);
    write_vector(writer, value.pushConstants,
                 [](Writer& output, const PushConstantRange& range) {
                     write_enum(output, range.stage);
                     output.u32(range.offset);
                     output.u32(range.size);
                 });
    write_vector(writer, value.specializationConstants, write_specialization);
    write_vector(writer, value.interfaces,
                 [](Writer& output, const ShaderInterfaceVariable& variable) {
                     write_enum(output, variable.stage);
                     output.u32(variable.location);
                     output.string(variable.name);
                     output.string(variable.type);
                     output.string(variable.interpolation);
                 });
    writer.u32(value.requiredWorkgroupSize.width);
    writer.u32(value.requiredWorkgroupSize.height);
    writer.u32(value.requiredWorkgroupSize.depth);
    writer.u32(value.preferredWorkgroupSize.width);
    writer.u32(value.preferredWorkgroupSize.height);
    writer.u32(value.preferredWorkgroupSize.depth);
}

[[nodiscard]] bool read_reflection(Reader& reader, ShaderReflection& value) {
    return read_vector(reader, value.bindings, read_binding) &&
           read_vector(reader, value.pushConstants,
                       [](Reader& input, PushConstantRange& range) {
                           return read_enum(input, range.stage,
                                            ShaderStage::compute) &&
                                  input.u32(range.offset) && input.u32(range.size);
                       }) &&
           read_vector(reader, value.specializationConstants,
                       read_specialization) &&
           read_vector(reader, value.interfaces,
                       [](Reader& input, ShaderInterfaceVariable& variable) {
                           return read_enum(input, variable.stage,
                                            ShaderStage::compute) &&
                                  input.u32(variable.location) &&
                                  input.string(variable.name) &&
                                  input.string(variable.type) &&
                                  input.string(variable.interpolation);
                       }) &&
           reader.u32(value.requiredWorkgroupSize.width) &&
           reader.u32(value.requiredWorkgroupSize.height) &&
           reader.u32(value.requiredWorkgroupSize.depth) &&
           reader.u32(value.preferredWorkgroupSize.width) &&
           reader.u32(value.preferredWorkgroupSize.height) &&
           reader.u32(value.preferredWorkgroupSize.depth);
}

[[nodiscard]] std::vector<std::byte> write_manifest(
    const ShaderPackageDesc& desc) {
    Writer writer;
    writer.string(desc.name);
    write_vector(writer, desc.requiredFeatures,
                 [](Writer& output, Feature value) {
                     write_enum(output, value);
                 });
    write_vector(writer, desc.permutations,
                 [](Writer& output, const ShaderPermutation& permutation) {
                     output.string(permutation.name);
                     write_vector(output, permutation.defines,
                                  [](Writer& target, const ShaderDefine& define) {
                                      target.string(define.name);
                                      target.string(define.value);
                                  });
                     write_vector(output, permutation.specializationConstants,
                                  write_specialization);
                 });
    write_vector(writer, desc.remaps,
                 [](Writer& output, const ShaderBindingRemap& remap) {
                     write_enum(output, remap.target);
                     write_enum(output, remap.stage);
                     output.u32(remap.group);
                     output.u32(remap.binding);
                     output.u32(remap.arrayElement);
                     output.u32(remap.nativeGroup);
                     output.u32(remap.nativeBinding);
                     output.u32(remap.nativeArrayElement);
                 });
    write_vector(writer, desc.sources,
                 [](Writer& output, const ShaderSourceRecord& source) {
                     output.string(source.path);
                     write_enum(output, source.language);
                     output.string(source.sha256);
                 });
    write_vector(writer, desc.compilers,
                 [](Writer& output, const ShaderCompilerRecord& compiler) {
                     output.string(compiler.name);
                     output.string(compiler.version);
                     output.string(compiler.revision);
                 });
    write_vector(writer, desc.diagnostics,
                 [](Writer& output, const ShaderPackageDiagnostic& diagnostic) {
                     write_enum(output, diagnostic.severity);
                     output.string(diagnostic.variant);
                     output.string(diagnostic.message);
                 });
    write_vector(writer, desc.variants,
                 [](Writer& output, const ShaderVariantDesc& variant) {
                     write_enum(output, variant.target);
                     write_enum(output, variant.format);
                     write_enum(output, variant.kind);
                     write_enum(output, variant.stage);
                     output.string(variant.entryPoint);
                     output.string(variant.permutation);
                     write_reflection(output, variant.reflection);
                 });
    return writer.take();
}

[[nodiscard]] bool read_manifest(Reader& reader, ShaderPackageDesc& desc) {
    return reader.string(desc.name) &&
           read_vector(reader, desc.requiredFeatures,
                       [](Reader& input, Feature& value) {
                           return read_enum(input, value,
                                            Feature::pipeline_cache);
                       }) &&
           read_vector(reader, desc.permutations,
                       [](Reader& input, ShaderPermutation& permutation) {
                           return input.string(permutation.name) &&
                                  read_vector(
                                      input, permutation.defines,
                                      [](Reader& source, ShaderDefine& define) {
                                          return source.string(define.name) &&
                                                 source.string(define.value);
                                      }) &&
                                  read_vector(
                                      input,
                                      permutation.specializationConstants,
                                      read_specialization);
                       }) &&
           read_vector(reader, desc.remaps,
                       [](Reader& input, ShaderBindingRemap& remap) {
                           return read_enum(input, remap.target,
                                            ShaderTarget::glsl_es) &&
                                  read_enum(input, remap.stage,
                                            ShaderStage::compute) &&
                                  input.u32(remap.group) &&
                                  input.u32(remap.binding) &&
                                  input.u32(remap.arrayElement) &&
                                  input.u32(remap.nativeGroup) &&
                                  input.u32(remap.nativeBinding) &&
                                  input.u32(remap.nativeArrayElement);
                       }) &&
           read_vector(reader, desc.sources,
                       [](Reader& input, ShaderSourceRecord& source) {
                           return input.string(source.path) &&
                                  read_enum(input, source.language,
                                            ShaderSourceLanguage::backend_native) &&
                                  input.string(source.sha256);
                       }) &&
           read_vector(reader, desc.compilers,
                       [](Reader& input, ShaderCompilerRecord& compiler) {
                           return input.string(compiler.name) &&
                                  input.string(compiler.version) &&
                                  input.string(compiler.revision);
                       }) &&
           read_vector(reader, desc.diagnostics,
                       [](Reader& input, ShaderPackageDiagnostic& diagnostic) {
                           return read_enum(input, diagnostic.severity,
                                            ShaderDiagnosticSeverity::error) &&
                                  input.string(diagnostic.variant) &&
                                  input.string(diagnostic.message);
                       }) &&
           read_vector(reader, desc.variants,
                       [](Reader& input, ShaderVariantDesc& variant) {
                           return read_enum(input, variant.target,
                                            ShaderTarget::glsl_es) &&
                                  read_enum(input, variant.format,
                                            ShaderByteFormat::metal_library) &&
                                  read_enum(input, variant.kind,
                                            ShaderVariantKind::native_override) &&
                                  read_enum(input, variant.stage,
                                            ShaderStage::compute) &&
                                  input.string(variant.entryPoint) &&
                                  input.string(variant.permutation) &&
                                  read_reflection(input, variant.reflection);
                       }) &&
           reader.remaining() == 0;
}

[[nodiscard]] std::vector<std::byte> encode(const ShaderPackageDesc& desc) {
    const auto manifest = write_manifest(desc);
    Writer writer;
    writer.append(packageMagic);
    writer.u16(containerVersion);
    writer.u16(desc.schemaMajor);
    writer.u16(desc.schemaMinor);
    writer.u16(0);
    writer.u64(static_cast<std::uint64_t>(manifest.size()));
    writer.u64(hash_bytes(manifest));
    writer.u32(static_cast<std::uint32_t>(desc.variants.size()));
    writer.u32(0);
    writer.append(manifest);
    for (const auto& variant : desc.variants) {
        writer.u64(static_cast<std::uint64_t>(variant.code.size()));
        writer.u64(hash_bytes(variant.code));
        writer.append(variant.code);
    }
    return writer.take();
}

[[nodiscard]] Result<ShaderPackageDesc> decode(
    std::span<const std::byte> bytes) {
    if (bytes.size() > maximumPackageBytes) {
        return invalid("shader package exceeds the runtime size limit");
    }
    Reader reader{bytes};
    std::span<const std::byte> magic;
    std::uint16_t version = 0;
    ShaderPackageDesc desc;
    std::uint16_t reserved16 = 0;
    std::uint64_t manifestSize = 0;
    std::uint64_t manifestHash = 0;
    std::uint32_t blobCount = 0;
    std::uint32_t reserved32 = 0;
    if (!reader.slice(packageMagic.size(), magic) ||
        !std::equal(magic.begin(), magic.end(), packageMagic.begin()) ||
        !reader.u16(version) || version != containerVersion ||
        !reader.u16(desc.schemaMajor) || !reader.u16(desc.schemaMinor) ||
        !reader.u16(reserved16) || reserved16 != 0 ||
        !reader.u64(manifestSize) || manifestSize > reader.remaining() ||
        !reader.u64(manifestHash) || !reader.u32(blobCount) ||
        blobCount > maximumRecords || !reader.u32(reserved32) ||
        reserved32 != 0) {
        return invalid("shader package header is corrupt or unsupported");
    }
    std::span<const std::byte> manifest;
    if (!reader.slice(static_cast<std::size_t>(manifestSize), manifest) ||
        hash_bytes(manifest) != manifestHash) {
        return invalid("shader package manifest integrity check failed");
    }
    Reader manifestReader{manifest};
    if (!read_manifest(manifestReader, desc) ||
        desc.variants.size() != blobCount) {
        return invalid("shader package manifest is corrupt");
    }
    for (auto& variant : desc.variants) {
        std::uint64_t blobSize = 0;
        std::uint64_t blobHash = 0;
        if (!reader.u64(blobSize) || blobSize > reader.remaining() ||
            blobSize > maximumPackageBytes || !reader.u64(blobHash)) {
            return invalid("shader package blob header is corrupt");
        }
        std::span<const std::byte> blob;
        if (!reader.slice(static_cast<std::size_t>(blobSize), blob) ||
            hash_bytes(blob) != blobHash) {
            return invalid("shader package blob integrity check failed");
        }
        variant.code.assign(blob.begin(), blob.end());
    }
    if (reader.remaining() != 0) {
        return invalid("shader package has trailing data");
    }
    return desc;
}

} // namespace

ShaderPackage::ShaderPackage(ShaderPackageDesc desc, std::vector<std::byte> bytes)
    : desc_(std::move(desc)), bytes_(std::move(bytes)) {}

Result<ShaderPackage> ShaderPackage::create(
    ShaderPackageDesc desc, const ShaderPackageOptions& options) {
    try {
        const auto status = normalize(desc, options);
        if (!status.ok()) {
            return status;
        }
        auto encoded = encode(desc);
        if (encoded.size() > maximumPackageBytes) {
            return invalid("shader package exceeds the runtime size limit");
        }
        return ShaderPackage{std::move(desc), std::move(encoded)};
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "shader package allocation failed");
    } catch (const std::length_error&) {
        return invalid("shader package contains an oversized field");
    }
}

Result<ShaderPackage> ShaderPackage::load(
    std::span<const std::byte> bytes, const ShaderPackageOptions& options) {
    try {
        auto decoded = decode(bytes);
        if (!decoded.ok()) {
            return decoded.status();
        }
        auto desc = std::move(decoded).value();
        const auto status = normalize(desc, options);
        if (!status.ok()) {
            return status;
        }
        auto canonical = encode(desc);
        if (canonical.size() != bytes.size() ||
            !std::equal(canonical.begin(), canonical.end(), bytes.begin())) {
            return invalid("shader package manifest is not canonical");
        }
        return ShaderPackage{std::move(desc), std::move(canonical)};
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "shader package allocation failed");
    } catch (const std::length_error&) {
        return invalid("shader package contains an oversized field");
    }
}

const ShaderPackageDesc& ShaderPackage::desc() const noexcept { return desc_; }

std::span<const std::byte> ShaderPackage::bytes() const noexcept {
    return bytes_;
}

const ShaderVariantDesc* ShaderPackage::find_variant(
    ShaderTarget target, std::string_view entryPoint, ShaderStage stage,
    std::string_view permutation) const noexcept {
    const ShaderVariantDesc* selected = nullptr;
    for (const auto& variant : desc_.variants) {
        if (variant.target != target || variant.entryPoint != entryPoint ||
            variant.stage != stage || variant.permutation != permutation) {
            continue;
        }
        if (selected == nullptr ||
            static_cast<std::uint8_t>(variant.kind) >
                static_cast<std::uint8_t>(selected->kind)) {
            selected = &variant;
        }
    }
    return selected;
}

Result<const ShaderVariantDesc*> ShaderPackage::select_variant(
    ShaderTarget target, std::string_view entryPoint, ShaderStage stage,
    std::string_view permutation) const {
    const auto* selected = find_variant(target, entryPoint, stage, permutation);
    if (selected != nullptr) {
        return selected;
    }
    try {
        return Status::failure(
            StatusCode::unsupported,
            "shader package '" + desc_.name + "' has no " +
                std::string{shader_target_name(target)} +
                " variant for entry '" + std::string{entryPoint} +
                "' and permutation '" + std::string{permutation} + "'");
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "shader variant diagnostic allocation failed");
    }
}

std::string_view shader_target_name(ShaderTarget target) noexcept {
    switch (target) {
    case ShaderTarget::spirv:
        return "spirv";
    case ShaderTarget::dxil:
        return "dxil";
    case ShaderTarget::metal_library:
        return "metallib";
    case ShaderTarget::metal_source:
        return "msl";
    case ShaderTarget::wgsl:
        return "wgsl";
    case ShaderTarget::glsl:
        return "glsl";
    case ShaderTarget::glsl_es:
        return "glsl-es";
    }
    return "unknown";
}

Result<ShaderTarget> shader_target_from_name(std::string_view name) {
    constexpr std::array targets{
        std::pair{std::string_view{"spirv"}, ShaderTarget::spirv},
        std::pair{std::string_view{"dxil"}, ShaderTarget::dxil},
        std::pair{std::string_view{"metallib"}, ShaderTarget::metal_library},
        std::pair{std::string_view{"msl"}, ShaderTarget::metal_source},
        std::pair{std::string_view{"wgsl"}, ShaderTarget::wgsl},
        std::pair{std::string_view{"glsl"}, ShaderTarget::glsl},
        std::pair{std::string_view{"glsl-es"}, ShaderTarget::glsl_es},
    };
    const auto found = std::find_if(targets.begin(), targets.end(),
                                    [&](const auto& item) {
                                        return item.first == name;
                                    });
    if (found == targets.end()) {
        return invalid("unknown shader target");
    }
    return found->second;
}

std::string_view shader_source_language_name(
    ShaderSourceLanguage language) noexcept {
    switch (language) {
    case ShaderSourceLanguage::slang:
        return "slang";
    case ShaderSourceLanguage::hlsl:
        return "hlsl";
    case ShaderSourceLanguage::glsl:
        return "glsl";
    case ShaderSourceLanguage::glsl_es:
        return "glsl-es";
    case ShaderSourceLanguage::wgsl:
        return "wgsl";
    case ShaderSourceLanguage::msl:
        return "msl";
    case ShaderSourceLanguage::spirv:
        return "spirv";
    case ShaderSourceLanguage::dxil:
        return "dxil";
    case ShaderSourceLanguage::metal_library:
        return "metallib";
    case ShaderSourceLanguage::backend_native:
        return "native";
    }
    return "unknown";
}

Result<ShaderSourceLanguage> shader_source_language_from_name(
    std::string_view name) {
    constexpr std::array languages{
        std::pair{std::string_view{"slang"}, ShaderSourceLanguage::slang},
        std::pair{std::string_view{"hlsl"}, ShaderSourceLanguage::hlsl},
        std::pair{std::string_view{"glsl"}, ShaderSourceLanguage::glsl},
        std::pair{std::string_view{"glsl-es"}, ShaderSourceLanguage::glsl_es},
        std::pair{std::string_view{"wgsl"}, ShaderSourceLanguage::wgsl},
        std::pair{std::string_view{"msl"}, ShaderSourceLanguage::msl},
        std::pair{std::string_view{"spirv"}, ShaderSourceLanguage::spirv},
        std::pair{std::string_view{"dxil"}, ShaderSourceLanguage::dxil},
        std::pair{std::string_view{"metallib"},
                  ShaderSourceLanguage::metal_library},
        std::pair{std::string_view{"native"},
                  ShaderSourceLanguage::backend_native},
    };
    const auto found = std::find_if(languages.begin(), languages.end(),
                                    [&](const auto& item) {
                                        return item.first == name;
                                    });
    if (found == languages.end()) {
        return invalid("unknown shader source language");
    }
    return found->second;
}

} // namespace truffle::rhi

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

#include "metal_adapter.hpp"
#include "compiler_inputs.hpp"
#include "digest.hpp"
#include "metal_offline_toolchain.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <sys/sysctl.h>

namespace truffle::shader::detail {
namespace {

using rhi::ResourceBinding;
using rhi::ResourceBindingType;
using rhi::ShaderInterfaceDirection;
using rhi::ShaderReflection;
using rhi::ShaderValueType;
using rhi::Status;
using rhi::StatusCode;

Status unsupported(std::string message) {
    return Status::failure(StatusCode::unsupported, std::move(message));
}

Status invalid(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

Status validate_includes(std::span<const Source> units) {
    for (const auto& unit : units) {
        std::string logical;
        logical.reserve(unit.text.size());
        for (std::size_t index = 0; index < unit.text.size(); ++index) {
            if (unit.text[index] == '\\' && index + 1 < unit.text.size() &&
                unit.text[index + 1] == '\n') {
                ++index;
            } else if (unit.text[index] == '\\' &&
                       index + 2 < unit.text.size() &&
                       unit.text[index + 1] == '\r' &&
                       unit.text[index + 2] == '\n') {
                index += 2;
            } else {
                logical.push_back(unit.text[index]);
            }
        }

        std::string sanitized;
        sanitized.reserve(logical.size());
        bool blockComment = false;
        char quote = '\0';
        for (std::size_t index = 0; index < logical.size(); ++index) {
            const char current = logical[index];
            const char next = index + 1 < logical.size()
                ? logical[index + 1] : '\0';
            if (blockComment) {
                if (current == '*' && next == '/') {
                    sanitized.push_back(' ');
                    ++index;
                    blockComment = false;
                } else if (current == '\n') {
                    sanitized.push_back('\n');
                }
                continue;
            }
            if (quote != '\0') {
                sanitized.push_back(current);
                if (current == '\\' && index + 1 < logical.size()) {
                    sanitized.push_back(logical[++index]);
                } else if (current == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (current == '/' && next == '*') {
                sanitized.push_back(' ');
                ++index;
                blockComment = true;
            } else if (current == '/' && next == '/') {
                sanitized.push_back(' ');
                ++index;
                while (index + 1 < logical.size() &&
                       logical[index + 1] != '\n') {
                    ++index;
                }
            } else {
                sanitized.push_back(current);
                if (current == '"' || current == '\'') quote = current;
            }
        }

        std::size_t offset = 0;
        while (offset < sanitized.size()) {
            const auto end = sanitized.find('\n', offset);
            const auto line = std::string_view{sanitized}.substr(
                offset, end == std::string::npos ? std::string::npos
                                                  : end - offset);
            auto cursor = line.find_first_not_of(" \t\r\f\v");
            if (cursor != std::string_view::npos &&
                (line[cursor] == '#' || line.substr(cursor, 2) == "%:")) {
                cursor += line[cursor] == '#' ? 1 : 2;
                while (cursor < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[cursor]))) {
                    ++cursor;
                }
                const auto directiveStart = cursor;
                while (cursor < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[cursor])) ||
                        line[cursor] == '_')) {
                    ++cursor;
                }
                const auto directive =
                    line.substr(directiveStart, cursor - directiveStart);
                while (cursor < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[cursor]))) {
                    ++cursor;
                }
                if (directive == "include") {
                    auto argument = line.substr(cursor);
                    const auto last =
                        argument.find_last_not_of(" \t\r\f\v");
                    if (last == std::string_view::npos) {
                        argument = {};
                    } else {
                        argument = argument.substr(0, last + 1);
                    }
                    if (argument != "<metal_stdlib>") {
                        return unsupported(
                            "Native MSL accepts only the built-in <metal_stdlib> include; "
                            "compose user code with CompileRequest::sourceUnits");
                    }
                } else if (directive == "import" ||
                           directive == "include_next" ||
                           directive == "include_macros") {
                    return unsupported(
                        "Native MSL imports and non-standard include directives are not supported; "
                        "compose user code with CompileRequest::sourceUnits");
                }
            }
            if (end == std::string::npos) break;
            offset = end + 1;
        }
    }
    return Status::success();
}

std::string utf8(NSString* value) {
    if (value == nil) return {};
    const char* characters = [value UTF8String];
    return characters ? std::string{characters} : std::string{};
}

std::string metal_error(NSError* error, std::string_view fallback) {
    if (error == nil) return std::string{fallback};
    auto message = utf8([error localizedDescription]);
    return message.empty() ? std::string{fallback} : std::move(message);
}

std::string operating_system_build() {
    std::array<char, 128> value{};
    std::size_t size = value.size();
    if (sysctlbyname("kern.osversion", value.data(), &size, nullptr, 0) != 0) {
        return "unknown";
    }
    return value.data();
}

std::string runtime_compiler_version() {
    const auto version = utf8([[NSProcessInfo processInfo] operatingSystemVersionString]);
    return version.empty() ? "macOS version unavailable" : version;
}

std::string runtime_compiler_revision() {
    return "metal-runtime;os-build=" + operating_system_build() +
           ";msl=2.1;reflection-api=macos26";
}

rhi::Result<std::vector<Source>> resolve_units(
    const CompileRequest& request) {
    if (request.source.identity.empty() || request.source.text.empty() ||
        request.source.text.find('\0') != std::string::npos) {
        return invalid("Native MSL requires non-empty root source identity/content without NUL bytes");
    }
    std::vector<Source> units;
    units.reserve(request.sourceUnits.size() + 1);
    std::set<std::string> identities;
    identities.insert(request.source.identity);
    for (const auto& requested : request.sourceUnits) {
        if (requested.empty()) {
            return invalid("Native MSL source-unit requests must not be empty");
        }
        rhi::Result<Source> resolved = [&]() -> rhi::Result<Source> {
            if (request.resolver) {
                return request.resolver(requested, request.source.identity, false);
            }
            auto local = read_source(
                (std::filesystem::path{request.source.identity}.parent_path() /
                 requested).string());
            if (local.ok()) return local;
            for (const auto& root : request.includeRoots) {
                auto result = read_source(
                    (std::filesystem::path{root} / requested).string());
                if (result.ok()) return result;
            }
            return invalid("Missing native MSL source unit '" + requested +
                           "' from " + request.source.identity);
        }();
        if (!resolved.ok()) return resolved.status();
        auto source = std::move(resolved).value();
        if (source.identity.empty() || source.text.empty() ||
            source.text.find('\0') != std::string::npos) {
            return invalid("Native MSL source unit has invalid identity or content: " +
                           requested);
        }
        if (!identities.insert(source.identity).second) {
            return invalid("Native MSL source units resolve to a duplicate identity: " +
                           source.identity);
        }
        units.push_back(std::move(source));
    }
    units.push_back(request.source);
    return units;
}

std::string quoted_line_identity(std::string_view identity) {
    std::string result;
    result.reserve(identity.size());
    for (const char character : identity) {
        if (character == '\\' || character == '"') result.push_back('\\');
        if (character == '\n' || character == '\r' || character == '\0') {
            result += '?';
        } else {
            result.push_back(character);
        }
    }
    return result;
}

std::string compose_source(std::span<const Source> units) {
    std::string result;
    for (const auto& unit : units) {
        result += "#line 1 \"";
        result += quoted_line_identity(unit.identity);
        result += "\"\n";
        result += unit.text;
        if (!unit.text.ends_with('\n')) result.push_back('\n');
    }
    return result;
}

std::string configured_source(
    std::span<const Source> units,
    const std::vector<rhi::ShaderDefine>& defines) {
    std::string result;
    for (const auto& define : defines) {
        result += "#define ";
        result += define.name;
        result += ' ';
        result += define.value;
        result += '\n';
    }
    result += compose_source(units);
    return result;
}

std::string canonical_type(std::string_view type) {
    if (type == "float2") return "vec2";
    if (type == "float3") return "vec3";
    if (type == "float4") return "vec4";
    if (type == "int2") return "ivec2";
    if (type == "int3") return "ivec3";
    if (type == "int4") return "ivec4";
    if (type == "uint2") return "uvec2";
    if (type == "uint3") return "uvec3";
    if (type == "uint4") return "uvec4";
    return std::string{type};
}

std::string_view msl_interface_type(MTLDataType type) {
    switch (type) {
    case MTLDataTypeFloat: return "float";
    case MTLDataTypeFloat2: return "vec2";
    case MTLDataTypeFloat3: return "vec3";
    case MTLDataTypeFloat4: return "vec4";
    case MTLDataTypeInt: return "int";
    case MTLDataTypeInt2: return "ivec2";
    case MTLDataTypeInt3: return "ivec3";
    case MTLDataTypeInt4: return "ivec4";
    case MTLDataTypeUInt: return "uint";
    case MTLDataTypeUInt2: return "uvec2";
    case MTLDataTypeUInt3: return "uvec3";
    case MTLDataTypeUInt4: return "uvec4";
    default: return {};
    }
}

std::optional<ShaderValueType> shader_value_type(MTLDataType type) {
    switch (type) {
    case MTLDataTypeBool: return ShaderValueType::boolean;
    case MTLDataTypeInt: return ShaderValueType::sint32;
    case MTLDataTypeUInt: return ShaderValueType::uint32;
    case MTLDataTypeFloat: return ShaderValueType::float32;
    default: return std::nullopt;
    }
}

void reflection_key(std::string& key, const ShaderReflection& reflection) {
    auto bindings = reflection.bindings;
    std::sort(bindings.begin(), bindings.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{static_cast<unsigned>(lhs.stage), lhs.group,
                          lhs.binding, static_cast<unsigned>(lhs.type), lhs.name} <
               std::tuple{static_cast<unsigned>(rhs.stage), rhs.group,
                          rhs.binding, static_cast<unsigned>(rhs.type), rhs.name};
    });
    field(key, bindings.size());
    for (const auto& binding : bindings) {
        field(key, binding.name);
        field(key, static_cast<unsigned>(binding.stage));
        field(key, static_cast<unsigned>(binding.type));
        field(key, binding.group);
        field(key, binding.binding);
        field(key, binding.arrayCount);
        field(key, binding.minimumSize);
        field(key, binding.readOnly ? 1 : 0);
    }
    auto pushConstants = reflection.pushConstants;
    std::sort(pushConstants.begin(), pushConstants.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{static_cast<unsigned>(lhs.stage), lhs.offset, lhs.size} <
               std::tuple{static_cast<unsigned>(rhs.stage), rhs.offset, rhs.size};
    });
    field(key, pushConstants.size());
    for (const auto& range : pushConstants) {
        field(key, static_cast<unsigned>(range.stage));
        field(key, range.offset);
        field(key, range.size);
    }
    auto constants = reflection.specializationConstants;
    std::sort(constants.begin(), constants.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.id, lhs.name} < std::tuple{rhs.id, rhs.name};
    });
    field(key, constants.size());
    for (const auto& constant : constants) {
        field(key, constant.id);
        field(key, constant.name);
        field(key, static_cast<unsigned>(constant.type));
        field(key, constant.defaultValueBits);
    }
    auto interfaces = reflection.interfaces;
    std::sort(interfaces.begin(), interfaces.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{static_cast<unsigned>(lhs.stage),
                          static_cast<unsigned>(lhs.direction), lhs.location} <
               std::tuple{static_cast<unsigned>(rhs.stage),
                          static_cast<unsigned>(rhs.direction), rhs.location};
    });
    field(key, interfaces.size());
    for (const auto& interface : interfaces) {
        field(key, static_cast<unsigned>(interface.stage));
        field(key, static_cast<unsigned>(interface.direction));
        field(key, interface.location);
        field(key, interface.name);
        field(key, interface.type);
        field(key, interface.interpolation);
    }
    field(key, reflection.requiredWorkgroupSize.width);
    field(key, reflection.requiredWorkgroupSize.height);
    field(key, reflection.requiredWorkgroupSize.depth);
    field(key, reflection.preferredWorkgroupSize.width);
    field(key, reflection.preferredWorkgroupSize.height);
    field(key, reflection.preferredWorkgroupSize.depth);
    for (const auto& id : reflection.workgroupSpecializationIds) {
        field(key, id ? std::to_string(*id) : "-");
    }
}

Status validate_stage(const ShaderReflection& reflection,
                      rhi::ShaderStage stage) {
    for (const auto& binding : reflection.bindings) {
        if (binding.stage != stage || binding.name.empty()) {
            return invalid("Native MSL resource declarations require names and the requested stage");
        }
    }
    for (const auto& range : reflection.pushConstants) {
        if (range.stage != stage) {
            return invalid("Native MSL push-constant declarations must use the requested stage");
        }
    }
    for (const auto& constant : reflection.specializationConstants) {
        if (constant.name.empty()) {
            return invalid("Native MSL specialization declarations require names");
        }
        if (constant.type == ShaderValueType::boolean &&
            constant.defaultValueBits > 1) {
            return invalid("Native MSL boolean specialization defaults must be zero or one");
        }
    }
    for (const auto& interface : reflection.interfaces) {
        if (interface.stage != stage) {
            return invalid("Native MSL IO declarations must use the requested stage");
        }
    }
    return Status::success();
}

Status validate_inputs(id<MTLFunction> function,
                       const ShaderReflection& declaration,
                       rhi::ShaderStage stage) {
    if (stage == rhi::ShaderStage::compute) return Status::success();
    NSArray<MTLAttribute*>* attributes = [function stageInputAttributes];
    if (attributes == nil) {
        if (stage == rhi::ShaderStage::vertex &&
            std::any_of(
                declaration.interfaces.begin(), declaration.interfaces.end(),
                [](const auto& interface) {
                    return interface.direction == ShaderInterfaceDirection::input;
                })) {
            return unsupported("Metal did not provide vertex stage-input reflection");
        }
        return Status::success();
    }
    std::vector<std::tuple<std::uint32_t, std::string, std::string>> actual;
    for (MTLAttribute* attribute in attributes) {
        if (![attribute isActive]) continue;
        const auto type = msl_interface_type([attribute attributeType]);
        if (type.empty()) {
            return unsupported("Native MSL vertex input type is not representable");
        }
        actual.emplace_back(
            static_cast<std::uint32_t>([attribute attributeIndex]),
            utf8([attribute name]), std::string{type});
    }
    std::vector<std::tuple<std::uint32_t, std::string, std::string>> expected;
    for (const auto& interface : declaration.interfaces) {
        if (interface.direction == ShaderInterfaceDirection::input) {
            expected.emplace_back(interface.location, interface.name,
                                  canonical_type(interface.type));
        }
    }
    const auto order = [](const auto& lhs, const auto& rhs) {
        return std::get<0>(lhs) < std::get<0>(rhs);
    };
    std::sort(actual.begin(), actual.end(), order);
    std::sort(expected.begin(), expected.end(), order);
    if (actual != expected) {
        return invalid("Native MSL stage-input locations/types disagree with the interface declaration");
    }
    return Status::success();
}

struct NativeBinding {
    std::string name;
    ResourceBindingType type = ResourceBindingType::buffer;
    std::uint32_t index = 0;
    std::uint32_t arrayCount = 1;
    std::size_t dataSize = 0;
    bool readOnly = false;
};

bool has_nested_resources(MTLStructType* structure, std::size_t depth = 0) {
    if (structure == nil || depth > 8) return depth > 8;
    for (MTLStructMember* member in [structure members]) {
        const auto type = [member dataType];
        if (type == MTLDataTypeTexture || type == MTLDataTypeSampler ||
            [member textureReferenceType] != nil ||
            [member pointerType] != nil) {
            return true;
        }
        if (type == MTLDataTypeStruct &&
            has_nested_resources([member structType], depth + 1)) {
            return true;
        }
        if (type == MTLDataTypeArray) {
            MTLArrayType* array = [member arrayType];
            if (array == nil || [array elementType] == MTLDataTypeTexture ||
                [array elementType] == MTLDataTypeSampler ||
                [array elementType] == MTLDataTypePointer ||
                ([array elementType] == MTLDataTypeStruct &&
                 has_nested_resources([array elementStructType], depth + 1))) {
                return true;
            }
        }
    }
    return false;
}

rhi::Result<NativeBinding> native_binding(id<MTLBinding> binding) {
    NativeBinding result;
    result.name = utf8([binding name]);
    result.index = static_cast<std::uint32_t>([binding index]);
    switch ([binding type]) {
    case MTLBindingTypeBuffer: {
        id<MTLBufferBinding> buffer = (id<MTLBufferBinding>)binding;
        MTLPointerType* pointerType = [buffer bufferPointerType];
        if ((pointerType != nil && [pointerType elementIsArgumentBuffer]) ||
            has_nested_resources([buffer bufferStructType])) {
            return unsupported("Native MSL argument buffers are not supported");
        }
        result.type = ResourceBindingType::buffer;
        result.dataSize = [buffer bufferDataSize];
        result.readOnly = [binding access] == MTLBindingAccessReadOnly;
        break;
    }
    case MTLBindingTypeTexture: {
        id<MTLTextureBinding> texture = (id<MTLTextureBinding>)binding;
        if ([texture textureType] != MTLTextureType2D ||
            [texture textureDataType] != MTLDataTypeFloat) {
            return unsupported("Native MSL initially supports only 2D float textures");
        }
        result.type = ResourceBindingType::texture;
        result.arrayCount = static_cast<std::uint32_t>([texture arrayLength]);
        result.readOnly = [binding access] == MTLBindingAccessReadOnly;
        break;
    }
    case MTLBindingTypeSampler:
        result.type = ResourceBindingType::sampler;
        result.readOnly = true;
        break;
    default:
        return unsupported("Native MSL resource kind is not represented by ShaderReflection");
    }
    if (result.name.empty()) {
        return unsupported("Metal reflection did not provide a resource name");
    }
    return result;
}

Status validate_resources(MTLFunctionReflection* metalReflection,
                          const ShaderReflection& declaration,
                          rhi::ShaderStage stage,
                          std::vector<rhi::ShaderBindingRemap>& remaps,
                          rhi::ShaderTarget target) {
    std::map<std::string, NativeBinding> actual;
    bool hasPushBuffer = false;
    std::size_t pushBufferSize = 0;
    for (id<MTLBinding> reflected in [metalReflection bindings]) {
        auto native = native_binding(reflected);
        if (!native.ok()) return native.status();
        auto value = std::move(native).value();
        if (value.type == ResourceBindingType::buffer && value.index == 30) {
            if (!value.readOnly) {
                return invalid(
                    "Native MSL push-data buffer(30) must be read-only");
            }
            if (hasPushBuffer) {
                return unsupported("Native MSL declares more than one Metal push-data buffer");
            }
            hasPushBuffer = true;
            pushBufferSize = value.dataSize;
            continue;
        }
        const auto [position, inserted] =
            actual.emplace(value.name, std::move(value));
        if (!inserted) {
            return unsupported("Metal reflection produced an ambiguous resource name: " +
                               position->first);
        }
    }

    std::size_t requiredPushSize = 0;
    for (const auto& range : declaration.pushConstants) {
        requiredPushSize = std::max(
            requiredPushSize,
            static_cast<std::size_t>(range.offset) + range.size);
    }
    if (requiredPushSize != 0 &&
        (!hasPushBuffer || pushBufferSize < requiredPushSize)) {
        return invalid("Native MSL buffer(30) does not cover the declared push-constant ranges");
    }
    if (hasPushBuffer && requiredPushSize == 0) {
        return unsupported("Metal buffer(30) is reserved for declared push constants");
    }

    std::set<std::tuple<ResourceBindingType, std::uint32_t>> occupied;
    for (const auto& resource : declaration.bindings) {
        const auto found = actual.find(resource.name);
        if (found == actual.end()) {
            return invalid("Native MSL resource is absent from Metal reflection: " +
                           resource.name);
        }
        const auto& reflected = found->second;
        if (resource.type != reflected.type ||
            resource.arrayCount != reflected.arrayCount ||
            resource.readOnly != reflected.readOnly) {
            return invalid("Native MSL resource type/access/count disagrees with the interface declaration: " +
                           resource.name);
        }
        if (resource.minimumSize < reflected.dataSize) {
            return invalid("Native MSL buffer minimum size is smaller than Metal's reflected data size: " +
                           resource.name);
        }
        if ((reflected.type == ResourceBindingType::buffer &&
             (reflected.index < 16 || reflected.index >= 30)) ||
            (reflected.type == ResourceBindingType::texture &&
             reflected.index + reflected.arrayCount - 1 > 127) ||
            (reflected.type == ResourceBindingType::sampler &&
             (reflected.index > 15 || resource.arrayCount != 1))) {
            return unsupported("Native MSL resource index/count is outside the supported RHI Metal slots");
        }
        for (std::uint32_t element = 0; element < resource.arrayCount; ++element) {
            if (!occupied.emplace(reflected.type, reflected.index + element).second) {
                return unsupported("Native MSL resources overlap a Metal binding index");
            }
        }
        for (std::uint32_t element = 0; element < resource.arrayCount; ++element) {
            remaps.push_back({
                .target = target,
                .stage = stage,
                .group = resource.group,
                .binding = resource.binding,
                .arrayElement = element,
                .nativeGroup = 0,
                .nativeBinding = reflected.index,
                .nativeArrayElement = element,
            });
        }
        actual.erase(found);
    }
    if (!actual.empty()) {
        return invalid("Metal resource reflection contains bindings missing from the interface declaration: " +
                       actual.begin()->first);
    }
    return Status::success();
}

Status validate_specializations(id<MTLFunction> function,
                                const ShaderReflection& declaration) {
    const auto* constants = [function functionConstantsDictionary];
    std::map<std::uint32_t, MTLFunctionConstant*> actual;
    for (NSString* name in constants) {
        MTLFunctionConstant* constant = constants[name];
        const auto id = static_cast<std::uint32_t>([constant index]);
        if (!actual.emplace(id, constant).second) {
            return unsupported("Metal reflection contains duplicate function-constant IDs");
        }
    }
    if (actual.size() != declaration.specializationConstants.size()) {
        return invalid("Native MSL function-constant count disagrees with the interface declaration");
    }
    for (const auto& expected : declaration.specializationConstants) {
        const auto found = actual.find(expected.id);
        if (found == actual.end()) {
            return invalid("Native MSL function constant is absent from Metal reflection: " +
                           std::to_string(expected.id));
        }
        const auto type = shader_value_type([found->second type]);
        const auto name = utf8([found->second name]);
        if (!type || *type != expected.type || name != expected.name) {
            return invalid("Native MSL function-constant ID/name/type disagrees with the interface declaration");
        }
        if (![found->second required]) {
            return unsupported(
                "Native MSL function constants with compiler-provided defaults "
                "are unsupported; declare required constants with defaults in "
                "the typed interface declaration");
        }
        actual.erase(found);
    }
    if (!actual.empty()) {
        return invalid("Metal function-constant reflection contains undeclared IDs");
    }
    return Status::success();
}

std::string define_preamble(const std::vector<rhi::ShaderDefine>& defines) {
    std::string result;
    for (const auto& define : defines) {
        result += define.name + "=" + define.value + "\n";
    }
    return result;
}

Status validate_native_reflection(
    id<MTLLibrary> library, id<MTLFunction> function,
    const CompileRequest& request, std::vector<rhi::ShaderBindingRemap>& remaps,
    std::string_view artifact) {
    if (@available(macOS 26.0, *)) {
        const auto expectedType =
            request.stage == rhi::ShaderStage::vertex ? MTLFunctionTypeVertex :
            request.stage == rhi::ShaderStage::fragment ? MTLFunctionTypeFragment :
            MTLFunctionTypeKernel;
        if ([function functionType] != expectedType) {
            return invalid("Native MSL entry point has the wrong Metal function stage");
        }
        const auto inputStatus = validate_inputs(
            function, *request.interfaceDeclaration, request.stage);
        if (!inputStatus.ok()) return inputStatus;
        auto* name = [[NSString alloc]
            initWithBytes:request.entryPoint.data()
                   length:request.entryPoint.size()
                 encoding:NSUTF8StringEncoding];
        if (name == nil) {
            return invalid("Native MSL entry point name is not valid UTF-8");
        }
        MTLFunctionReflection* reflection =
            [library reflectionForFunctionWithName:name];
        [name release];
        if (reflection == nil) {
            return unsupported("Metal did not provide complete function-binding reflection for " +
                               std::string{artifact});
        }
        const auto resourceStatus = validate_resources(
            reflection, *request.interfaceDeclaration, request.stage,
            remaps, request.target);
        if (!resourceStatus.ok()) return resourceStatus;
        return validate_specializations(function, *request.interfaceDeclaration);
    }
    return unsupported("Native MSL reflection requires macOS 26 or newer");
}

} // namespace

bool native_msl_available() noexcept {
    @autoreleasepool {
        if (@available(macOS 26.0, *)) {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil) return false;
            [device release];
            return true;
        }
    }
    return false;
}

rhi::Result<Compilation> compile_msl(
    const CompileRequest& request, std::vector<Compilation>& cache) {
    if (!request.interfaceDeclaration) {
        return invalid("Native MSL requires an explicit typed ShaderReflection interface declaration");
    }
    if (request.name.empty() || request.entryPoint.empty()) {
        return invalid("Native MSL requires a package name and entry point");
    }
    if (request.generateDebugInfo &&
        request.target == rhi::ShaderTarget::metal_source) {
        return unsupported("The in-process Metal source API does not expose a debug-info option");
    }
    std::optional<OfflineMetalToolchain> offlineToolchain;
    if (request.target == rhi::ShaderTarget::metal_library) {
        auto toolchain = offline_metallib_toolchain();
        if (!toolchain.ok()) return toolchain.status();
        offlineToolchain = std::move(toolchain).value();
    }
    const auto stageStatus =
        validate_stage(*request.interfaceDeclaration, request.stage);
    if (!stageStatus.ok()) return stageStatus;
    auto definesResult = normalized_defines(request.defines);
    if (!definesResult.ok()) return definesResult.status();
    const auto defines = std::move(definesResult).value();
    auto sourcesResult = resolve_units(request);
    if (!sourcesResult.ok()) return sourcesResult.status();
    auto sources = std::move(sourcesResult).value();
    if (request.target == rhi::ShaderTarget::metal_source) {
        const auto includeStatus = validate_includes(sources);
        if (!includeStatus.ok()) return includeStatus;
    }
    const auto composed = configured_source(sources, defines);

    std::string compilerVersion = runtime_compiler_version();
    std::string compilerRevision = runtime_compiler_revision();
    std::string toolVersion;
    std::string toolLinkerVersion;
    std::string toolSdkVersion;
    if (request.target == rhi::ShaderTarget::metal_library) {
        const auto dependencyStatus =
            validate_metallib_dependencies(*offlineToolchain, composed);
        if (!dependencyStatus.ok()) return dependencyStatus;
        toolVersion = offlineToolchain->compilerVersion;
        toolLinkerVersion = offlineToolchain->linkerVersion;
        toolSdkVersion = offlineToolchain->sdkVersion;
        compilerVersion = toolVersion;
        compilerRevision += ";offline=" + toolVersion + ";metallib=" +
                            toolLinkerVersion +
                            ";target=air64-apple-macos13.0;msl=2.1;sdk=" +
                            offlineToolchain->sdkVersion;
    }
    std::string key;
    field(key, "truffle-native-msl=1");
    field(key, compilerVersion);
    field(key, compilerRevision);
    field(key, request.name);
    field(key, request.entryPoint);
    field(key, static_cast<unsigned>(request.stage));
    field(key, static_cast<unsigned>(request.target));
    field(key, static_cast<unsigned>(request.metalTarget.version));
    field(key, request.generateDebugInfo ? 1 : 0);
    field(key, request.allowExperimentalMetalGeneration ? 1 : 0);
    field(key, define_preamble(defines));
    for (const auto& root : request.includeRoots) field(key, root);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (index < request.sourceUnits.size()) {
            field(key, request.sourceUnits[index]);
        } else {
            field(key, "root");
        }
        field(key, sources[index].identity);
        field(key, sources[index].text);
    }
    reflection_key(key, *request.interfaceDeclaration);
    Compilation result;
    result.compiler = {
        request.target == rhi::ShaderTarget::metal_library
            ? "Apple Metal offline compiler" : "Apple Metal runtime compiler",
        compilerVersion,
        compilerRevision,
    };
    result.cacheKey = sha256(key);
    for (const auto& source : sources) {
        result.dependencies.push_back(
            {source.identity, sha256(source.text)});
    }
    for (const auto& cached : cache) {
        if (cached.cacheKey == result.cacheKey) {
            auto hit = cached;
            hit.cacheHit = true;
            return hit;
        }
    }

    @autoreleasepool {
        if (@available(macOS 26.0, *)) {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device == nil) {
                return unsupported("No Metal device is available for native MSL compilation");
            }
            auto* source = [[NSString alloc]
                initWithBytes:composed.data()
                       length:composed.size()
                     encoding:NSUTF8StringEncoding];
            if (source == nil) {
                [device release];
                return invalid("Native MSL source is not valid UTF-8");
            }
            auto* options = [[MTLCompileOptions alloc] init];
            options.languageVersion = MTLLanguageVersion2_1;
            NSError* error = nil;
            id<MTLLibrary> library =
                [device newLibraryWithSource:source options:options error:&error];
            [source release];
            [options release];
            if (library == nil) {
                [device release];
                const auto message = metal_error(
                    error, "Native Metal source compilation failed");
                return invalid(message.substr(0, 16384));
            }
            auto* entryName = [[NSString alloc]
                initWithBytes:request.entryPoint.data()
                       length:request.entryPoint.size()
                     encoding:NSUTF8StringEncoding];
            if (entryName == nil) {
                [library release];
                [device release];
                return invalid("Native MSL entry point is not valid UTF-8");
            }
            id<MTLFunction> function = [library newFunctionWithName:entryName];
            [entryName release];
            if (function == nil) {
                [library release];
                [device release];
                return invalid("Native MSL entry point was not found");
            }
            std::vector<rhi::ShaderBindingRemap> runtimeRemaps;
            const auto reflectionStatus = validate_native_reflection(
                library, function, request, runtimeRemaps, "runtime source");
            [function release];
            [library release];
            if (!reflectionStatus.ok()) {
                [device release];
                return reflectionStatus;
            }

            std::vector<std::byte> code;
            std::string diagnostics;
            auto remaps = std::move(runtimeRemaps);
            if (request.target == rhi::ShaderTarget::metal_library) {
                auto offline = compile_metallib(*offlineToolchain, composed,
                                                request.generateDebugInfo);
                if (!offline.ok()) {
                    [device release];
                    return offline.status();
                }
                auto output = std::move(offline).value();
                code = std::move(output.bytes);
                diagnostics = std::move(output.diagnostics);
                auto currentToolchain = offline_metallib_toolchain();
                if (!currentToolchain.ok() ||
                    currentToolchain.value().compilerVersion != toolVersion ||
                    currentToolchain.value().linkerVersion != toolLinkerVersion ||
                    currentToolchain.value().sdkVersion != toolSdkVersion ||
                    output.compilerVersion != toolVersion ||
                    output.linkerVersion != toolLinkerVersion ||
                    output.sdkVersion != toolSdkVersion) {
                    [device release];
                    return rhi::Status::failure(
                        StatusCode::backend_error,
                        "Apple Metal Toolchain identity changed during compilation; "
                        "retry to avoid stale cache provenance");
                }
                dispatch_data_t data = dispatch_data_create(
                    code.data(), code.size(), nullptr,
                    DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                if (data == nullptr) {
                    [device release];
                    return rhi::Status::failure(
                        StatusCode::backend_error,
                        "Unable to expose offline metallib bytes to Metal reflection");
                }
                NSError* loadError = nil;
                id<MTLLibrary> offlineLibrary =
                    [device newLibraryWithData:data error:&loadError];
                dispatch_release(data);
                if (offlineLibrary == nil) {
                    const auto message = metal_error(
                        loadError, "Metal could not load the emitted metallib");
                    [device release];
                    return invalid(message.substr(0, 16384));
                }
                auto* offlineEntryName = [[NSString alloc]
                    initWithBytes:request.entryPoint.data()
                           length:request.entryPoint.size()
                         encoding:NSUTF8StringEncoding];
                if (offlineEntryName == nil) {
                    [offlineLibrary release];
                    [device release];
                    return invalid("Native MSL entry point is not valid UTF-8");
                }
                id<MTLFunction> offlineFunction =
                    [offlineLibrary newFunctionWithName:offlineEntryName];
                [offlineEntryName release];
                if (offlineFunction == nil) {
                    [offlineLibrary release];
                    [device release];
                    return invalid(
                        "Emitted metallib does not contain the requested entry point");
                }
                std::vector<rhi::ShaderBindingRemap> offlineRemaps;
                const auto offlineReflectionStatus = validate_native_reflection(
                    offlineLibrary, offlineFunction, request, offlineRemaps,
                    "emitted metallib");
                [offlineFunction release];
                [offlineLibrary release];
                if (!offlineReflectionStatus.ok()) {
                    [device release];
                    return offlineReflectionStatus;
                }
                remaps = std::move(offlineRemaps);
            } else {
                code.resize(composed.size());
                std::memcpy(code.data(), composed.data(), composed.size());
            }
            [device release];
            result.diagnostics = std::move(diagnostics);
            rhi::ShaderPackageDesc desc;
            desc.name = request.name;
            desc.compilers.push_back(result.compiler);
            for (const auto& dependency : result.dependencies) {
                desc.sources.push_back({
                    dependency.identity, rhi::ShaderSourceLanguage::msl,
                    dependency.sha256});
            }
            if (!defines.empty()) {
                const auto permutation = sha256(define_preamble(defines));
                desc.permutations.push_back({permutation, defines, {}});
            }
            if (!result.diagnostics.empty()) {
                desc.diagnostics.push_back({
                    rhi::ShaderDiagnosticSeverity::info,
                    std::string{rhi::shader_target_name(request.target)} + ":" +
                        std::to_string(static_cast<unsigned>(request.stage)) + ":" +
                        request.entryPoint,
                    result.diagnostics.substr(0, 4096)});
            }
            rhi::ShaderVariantDesc variant;
            variant.target = request.target;
            variant.format = request.target == rhi::ShaderTarget::metal_library
                ? rhi::ShaderByteFormat::metal_library
                : rhi::ShaderByteFormat::native_source;
            variant.kind = rhi::ShaderVariantKind::generated;
            variant.stage = request.stage;
            variant.entryPoint = request.entryPoint;
            variant.nativeEntryPoint = request.entryPoint;
            if (!defines.empty()) {
                variant.permutation = desc.permutations.front().name;
            }
            variant.reflection = *request.interfaceDeclaration;
            variant.code = std::move(code);
            desc.remaps = std::move(remaps);
            desc.variants.push_back(std::move(variant));
            auto package = rhi::ShaderPackage::create(
                std::move(desc),
                {.allowExperimentalMetalGeneration =
                     request.allowExperimentalMetalGeneration});
            if (!package.ok()) return package.status();
            result.package = std::move(package).value();
            cache.push_back(result);
            return result;
        }
    }
    return unsupported("Native MSL reflection requires macOS 26 or newer");
}

} // namespace truffle::shader::detail

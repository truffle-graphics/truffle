#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "metal_adapter.hpp"
#include "digest.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
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
#include <thread>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TRUFFLE_XCRUN_EXECUTABLE
#define TRUFFLE_XCRUN_EXECUTABLE "xcrun"
#endif

extern char** environ;

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
        std::size_t offset = 0;
        while (offset < unit.text.size()) {
            const auto end = unit.text.find('\n', offset);
            const auto line = std::string_view{unit.text}.substr(
                offset, end == std::string::npos ? std::string::npos
                                                  : end - offset);
            const auto first = line.find_first_not_of(" \t\r");
            auto marker = line.find('#');
            while (marker != std::string_view::npos) {
                auto tokenStart = marker + 1;
                while (tokenStart < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[tokenStart]))) {
                    ++tokenStart;
                }
                auto tokenEnd = tokenStart;
                while (tokenEnd < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[tokenEnd])) ||
                        line[tokenEnd] == '_')) {
                    ++tokenEnd;
                }
                if (line.substr(tokenStart, tokenEnd - tokenStart) == "include") {
                    auto argumentStart = line.find_first_not_of(
                        " \t\r", tokenEnd);
                    auto argument = argumentStart == std::string_view::npos
                        ? std::string_view{}
                        : line.substr(argumentStart);
                    const auto argumentEnd =
                        argument.find_last_not_of(" \t\r");
                    if (argumentEnd != std::string_view::npos) {
                        argument = argument.substr(0, argumentEnd + 1);
                    }
                    const bool builtInOnly =
                        first == marker &&
                        argument == "<metal_stdlib>";
                    if (!builtInOnly) {
                        return unsupported(
                            "Native MSL accepts only the built-in <metal_stdlib> include; "
                            "compose user code with CompileRequest::sourceUnits");
                    }
                }
                marker = line.find('#', marker + 1);
            }
            if (end == std::string::npos) break;
            offset = end + 1;
        }
    }
    return Status::success();
}

void field(std::string& output, std::string_view value) {
    output += std::to_string(value.size()) + ":";
    output += value;
}

template <typename Value>
    requires std::is_integral_v<Value>
void field(std::string& output, Value value) {
    field(output, std::to_string(value));
}

bool identifier(std::string_view value) {
    if (value.empty() ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) ||
          value.front() == '_')) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_';
    });
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

std::string single_line(std::string value) {
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    if (value.size() > 1024) value.resize(1024);
    return value;
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

std::vector<rhi::ShaderDefine> normalized_defines(
    const CompileRequest& request, Status& status) {
    auto defines = request.defines;
    std::sort(defines.begin(), defines.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
    });
    for (std::size_t index = 0; index < defines.size(); ++index) {
        const auto& define = defines[index];
        if (!identifier(define.name) ||
            define.value.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            (index != 0 && defines[index - 1].name == define.name)) {
            status = invalid("Invalid/duplicate shader define: " + define.name);
            return {};
        }
    }
    status = Status::success();
    return defines;
}

struct CommandOutput {
    int status = -1;
    std::string output;
    std::string error;
};

std::vector<char*> argv_for(std::vector<std::string>& arguments) {
    std::vector<char*> values;
    values.reserve(arguments.size() + 1);
    for (auto& argument : arguments) values.push_back(argument.data());
    values.push_back(nullptr);
    return values;
}

CommandOutput run_capture(std::vector<std::string> arguments) {
    CommandOutput result;
    int outputPipe[2]{-1, -1};
    if (pipe(outputPipe) != 0) {
        result.error = "Unable to create Metal tool output pipe";
        return result;
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        result.error = "Unable to initialize Metal tool process";
        return result;
    }
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
    auto argv = argv_for(arguments);
    pid_t child = 0;
    const int spawnResult = posix_spawn(&child, argv.front(), &actions, nullptr,
                                        argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(outputPipe[1]);
    if (spawnResult != 0) {
        close(outputPipe[0]);
        result.error = "Unable to launch Apple Metal tool: " +
                       std::string{std::strerror(spawnResult)};
        return result;
    }
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto count = read(outputPipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            result.output.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            result.error = "Unable to read Apple Metal tool output";
            break;
        }
    }
    close(outputPipe[0]);
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status)) {
        result.status = -1;
    } else {
        result.status = WEXITSTATUS(status);
    }
    if (result.status != 0 && result.error.empty()) result.error = result.output;
    return result;
}

struct OfflineResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;
};

struct StreamState {
    int source = -1;
    int output = -1;
    int metalErrors = -1;
    int metallibErrors = -1;
    const std::string* sourceText = nullptr;
    std::vector<std::byte> binary;
    std::string metalLog;
    std::string metallibLog;
    std::string ioError;
};

void close_descriptor(int& descriptor) {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}

void read_pipe(int descriptor, std::vector<std::byte>* binary,
               std::string* text) {
    std::array<std::byte, 8192> buffer{};
    for (;;) {
        const auto count = read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            if (binary) {
                binary->insert(binary->end(), buffer.begin(),
                               buffer.begin() + count);
            }
            if (text) {
                text->append(reinterpret_cast<const char*>(buffer.data()),
                             static_cast<std::size_t>(count));
            }
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
}

void write_source(int descriptor, std::string_view source) {
    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    std::size_t offset = 0;
    bool pipeClosed = false;
    while (offset < source.size()) {
        const auto count = write(descriptor, source.data() + offset,
                                 source.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            pipeClosed = errno == EPIPE;
            break;
        }
    }
    close(descriptor);
    if (pipeClosed) {
        sigset_t pending;
        if (sigpending(&pending) == 0 &&
            sigismember(&pending, SIGPIPE) == 1) {
            int signal = 0;
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

int add_child_actions(posix_spawn_file_actions_t& actions,
                      int input, int output, int errorOutput,
                      const std::array<int, 10>& descriptors) {
    if (posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, errorOutput, STDERR_FILENO) != 0) {
        return EINVAL;
    }
    for (const int descriptor : descriptors) {
        if (descriptor > STDERR_FILENO &&
            posix_spawn_file_actions_addclose(&actions, descriptor) != 0) {
            return EINVAL;
        }
    }
    return 0;
}

rhi::Result<OfflineResult> compile_metallib(
    const std::string& source, bool debugInfo) {
    int sourcePipe[2]{-1, -1};
    int airPipe[2]{-1, -1};
    int binaryPipe[2]{-1, -1};
    int metalErrorPipe[2]{-1, -1};
    int metallibErrorPipe[2]{-1, -1};
    if (pipe(sourcePipe) != 0 || pipe(airPipe) != 0 ||
        pipe(binaryPipe) != 0 || pipe(metalErrorPipe) != 0 ||
        pipe(metallibErrorPipe) != 0) {
        for (auto* pair : {sourcePipe, airPipe, binaryPipe,
                           metalErrorPipe, metallibErrorPipe}) {
            close_descriptor(pair[0]);
            close_descriptor(pair[1]);
        }
        return unsupported("Unable to create offline Metal compiler pipes");
    }

    const std::array<int, 10> descriptors{
        sourcePipe[0], sourcePipe[1], airPipe[0], airPipe[1],
        binaryPipe[0], binaryPipe[1], metalErrorPipe[0], metalErrorPipe[1],
        metallibErrorPipe[0], metallibErrorPipe[1]};
    std::vector<std::string> metalArguments{
        TRUFFLE_XCRUN_EXECUTABLE, "metal", "-x", "metal",
        "-std=macos-metal2.1", "-target", "air64-apple-macos13.0"};
    if (debugInfo) metalArguments.emplace_back("-g");
    metalArguments.insert(metalArguments.end(), {"-c", "-", "-o", "-"});
    auto metalArgv = argv_for(metalArguments);
    std::vector<std::string> metallibArguments{
        TRUFFLE_XCRUN_EXECUTABLE, "metallib", "-", "-o", "-"};
    auto metallibArgv = argv_for(metallibArguments);

    posix_spawn_file_actions_t metalActions;
    posix_spawn_file_actions_t metallibActions;
    const int metalActionsResult =
        posix_spawn_file_actions_init(&metalActions);
    const int metallibActionsResult =
        metalActionsResult == 0
            ? posix_spawn_file_actions_init(&metallibActions)
            : metalActionsResult;
    if (metalActionsResult != 0 || metallibActionsResult != 0) {
        if (metalActionsResult == 0) {
            posix_spawn_file_actions_destroy(&metalActions);
        }
        for (const int descriptor : descriptors) {
            if (descriptor >= 0) close(descriptor);
        }
        return unsupported("Unable to initialize offline Metal compiler processes");
    }
    add_child_actions(metalActions, sourcePipe[0], airPipe[1],
                      metalErrorPipe[1], descriptors);
    add_child_actions(metallibActions, airPipe[0], binaryPipe[1],
                      metallibErrorPipe[1], descriptors);
    pid_t metalChild = 0;
    pid_t metallibChild = 0;
    const int metalSpawn = posix_spawn(
        &metalChild, metalArgv.front(), &metalActions, nullptr,
        metalArgv.data(), environ);
    const int metallibSpawn = metalSpawn == 0
        ? posix_spawn(&metallibChild, metallibArgv.front(), &metallibActions,
                      nullptr, metallibArgv.data(), environ)
        : metalSpawn;
    posix_spawn_file_actions_destroy(&metalActions);
    posix_spawn_file_actions_destroy(&metallibActions);
    close_descriptor(sourcePipe[0]);
    close_descriptor(airPipe[0]);
    close_descriptor(airPipe[1]);
    close_descriptor(binaryPipe[1]);
    close_descriptor(metalErrorPipe[1]);
    close_descriptor(metallibErrorPipe[1]);
    if (metallibSpawn != 0) {
        close_descriptor(sourcePipe[1]);
        close_descriptor(binaryPipe[0]);
        close_descriptor(metalErrorPipe[0]);
        close_descriptor(metallibErrorPipe[0]);
        if (metalSpawn == 0) {
            kill(metalChild, SIGTERM);
            int ignored = 0;
            waitpid(metalChild, &ignored, 0);
        }
        return unsupported("Unable to launch Apple Metal Toolchain: " +
                           std::string{std::strerror(metallibSpawn)});
    }

    StreamState stream;
    stream.source = sourcePipe[1];
    stream.output = binaryPipe[0];
    stream.metalErrors = metalErrorPipe[0];
    stream.metallibErrors = metallibErrorPipe[0];
    stream.sourceText = &source;
    std::thread writer{[&] { write_source(stream.source, *stream.sourceText); }};
    std::thread reader{[&] {
        read_pipe(stream.output, &stream.binary, nullptr);
        close(stream.output);
    }};
    std::thread metalReader{[&] {
        read_pipe(stream.metalErrors, nullptr, &stream.metalLog);
        close(stream.metalErrors);
    }};
    std::thread metallibReader{[&] {
        read_pipe(stream.metallibErrors, nullptr, &stream.metallibLog);
        close(stream.metallibErrors);
    }};
    writer.join();
    reader.join();
    metalReader.join();
    metallibReader.join();
    int metalStatus = 0;
    int metallibStatus = 0;
    waitpid(metalChild, &metalStatus, 0);
    waitpid(metallibChild, &metallibStatus, 0);
    if (!WIFEXITED(metalStatus) || WEXITSTATUS(metalStatus) != 0 ||
        !WIFEXITED(metallibStatus) || WEXITSTATUS(metallibStatus) != 0) {
        auto diagnostics = stream.metalLog + stream.metallibLog;
        if (diagnostics.empty()) diagnostics = "Apple Metal offline compilation failed";
        return invalid("Apple Metal offline compilation failed: " +
                       diagnostics.substr(0, 16384));
    }
    if (stream.binary.empty()) {
        return rhi::Status::failure(StatusCode::backend_error,
                                    "metallib produced an empty library");
    }
    return OfflineResult{std::move(stream.binary),
                         (stream.metalLog + stream.metallibLog).substr(0, 16384)};
}

Status validate_native_reflection(
    id<MTLLibrary> library, id<MTLFunction> function,
    const CompileRequest& request, std::vector<rhi::ShaderBindingRemap>& remaps) {
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
            return unsupported("Metal did not provide complete function-binding reflection");
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

bool offline_metallib_available() noexcept {
#if defined(TRUFFLE_SHADER_HAS_METALLIB_COMPILER)
    if (access(TRUFFLE_XCRUN_EXECUTABLE, X_OK) != 0) return false;
    try {
        auto metal = run_capture(
            {TRUFFLE_XCRUN_EXECUTABLE, "--find", "metal"});
        auto metallib = run_capture(
            {TRUFFLE_XCRUN_EXECUTABLE, "--find", "metallib"});
        while (!metal.output.empty() &&
               (metal.output.back() == '\n' || metal.output.back() == '\r')) {
            metal.output.pop_back();
        }
        while (!metallib.output.empty() &&
               (metallib.output.back() == '\n' ||
                metallib.output.back() == '\r')) {
            metallib.output.pop_back();
        }
        return metal.status == 0 && metallib.status == 0 &&
               !metal.output.empty() && !metallib.output.empty() &&
               access(metal.output.c_str(), X_OK) == 0 &&
               access(metallib.output.c_str(), X_OK) == 0;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

rhi::Result<Compilation> compile_msl(
    const CompileRequest& request, std::vector<Compilation>& cache) {
    if (!request.interfaceDeclaration) {
        return invalid("Native MSL requires an explicit typed ShaderReflection interface declaration");
    }
    if (request.name.empty() || request.entryPoint.empty()) {
        return invalid("Native MSL requires a package name and entry point");
    }
    if (request.target == rhi::ShaderTarget::metal_library &&
        !offline_metallib_available()) {
        return unsupported("Offline metallib output requires the installed Apple Metal Toolchain");
    }
    if (request.generateDebugInfo &&
        request.target == rhi::ShaderTarget::metal_source) {
        return unsupported("The in-process Metal source API does not expose a debug-info option");
    }
    const auto stageStatus =
        validate_stage(*request.interfaceDeclaration, request.stage);
    if (!stageStatus.ok()) return stageStatus;
    Status defineStatus = Status::success();
    const auto defines = normalized_defines(request, defineStatus);
    if (!defineStatus.ok()) return defineStatus;
    auto sourcesResult = resolve_units(request);
    if (!sourcesResult.ok()) return sourcesResult.status();
    auto sources = std::move(sourcesResult).value();
    const auto includeStatus = validate_includes(sources);
    if (!includeStatus.ok()) return includeStatus;
    const auto composed = configured_source(sources, defines);

    std::string compilerVersion = runtime_compiler_version();
    std::string compilerRevision = runtime_compiler_revision();
    std::string toolVersion;
    std::string toolLinkerVersion;
    if (request.target == rhi::ShaderTarget::metal_library) {
        const auto metalVersion = run_capture(
            {TRUFFLE_XCRUN_EXECUTABLE, "metal", "--version"});
        const auto linkerVersion = run_capture(
            {TRUFFLE_XCRUN_EXECUTABLE, "metallib", "--version"});
        if (metalVersion.status != 0 || linkerVersion.status != 0) {
            return unsupported("Unable to query the installed Apple Metal Toolchain version");
        }
        toolVersion = single_line(metalVersion.output);
        toolLinkerVersion = single_line(linkerVersion.output);
        const auto firstLineEnd = metalVersion.output.find_first_of("\r\n");
        compilerVersion = single_line(
            metalVersion.output.substr(0, firstLineEnd));
        compilerRevision += ";offline=" + toolVersion + ";metallib=" +
                            toolLinkerVersion +
                            ";target=air64-apple-macos13.0;msl=2.1";
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
            [device release];
            if (library == nil) {
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
                return invalid("Native MSL entry point is not valid UTF-8");
            }
            id<MTLFunction> function = [library newFunctionWithName:entryName];
            [entryName release];
            if (function == nil) {
                [library release];
                return invalid("Native MSL entry point was not found");
            }
            std::vector<rhi::ShaderBindingRemap> remaps;
            const auto reflectionStatus = validate_native_reflection(
                library, function, request, remaps);
            [function release];
            [library release];
            if (!reflectionStatus.ok()) return reflectionStatus;

            std::vector<std::byte> code;
            std::string diagnostics;
            if (request.target == rhi::ShaderTarget::metal_library) {
                auto offline = compile_metallib(composed,
                                                request.generateDebugInfo);
                if (!offline.ok()) return offline.status();
                auto output = std::move(offline).value();
                code = std::move(output.bytes);
                diagnostics = std::move(output.diagnostics);
            } else {
                code.resize(composed.size());
                std::memcpy(code.data(), composed.data(), composed.size());
            }
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

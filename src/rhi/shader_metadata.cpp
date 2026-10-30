#include "shader_metadata.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace truffle::rhi::detail {
namespace {
Status invalid(const char* message) {
    return Status::failure(StatusCode::invalid_argument, message);
}

bool positive_integer(ShaderValueType type, std::uint32_t bits) {
    return bits != 0 && (type == ShaderValueType::uint32 ||
        (type == ShaderValueType::sint32 && bits <= 0x7fffffffu));
}
}

std::string_view interface_type(std::string_view type) noexcept {
    if (type == "float2") return "vec2";
    if (type == "float3") return "vec3";
    if (type == "float4") return "vec4";
    if (type == "int2") return "ivec2";
    if (type == "int3") return "ivec3";
    if (type == "int4") return "ivec4";
    if (type == "uint2") return "uvec2";
    if (type == "uint3") return "uvec3";
    if (type == "uint4") return "uvec4";
    return type;
}

Status validate_graphics_interfaces(const ShaderDesc* vertex, const ShaderDesc* fragment,
                                    const PipelineDesc& pipeline) {
    if (fragment) {
        for (const auto& input : fragment->interfaces) {
            if (input.direction != ShaderInterfaceDirection::input) continue;
            if (!vertex || std::none_of(vertex->interfaces.begin(), vertex->interfaces.end(), [&](const auto& output) {
                    return output.direction == ShaderInterfaceDirection::output &&
                           output.location == input.location &&
                           interface_type(output.type) == interface_type(input.type);
                }))
                return invalid("fragment input has no matching vertex output location/type");
        }
    }
    if (!vertex) return Status::success();
    for (const auto& input : vertex->interfaces) {
        if (input.direction != ShaderInterfaceDirection::input) continue;
        const auto type = interface_type(input.type);
        bool matched = false;
        for (const auto& buffer : pipeline.vertexBuffers)
            for (const auto& attribute : buffer.attributes) {
                if (attribute.location != input.location) continue;
                const bool unsignedFormat = attribute.format >= VertexFormat::uint32 &&
                                            attribute.format <= VertexFormat::uint32x4;
                const bool floatFormat = attribute.format >= VertexFormat::float32 &&
                                         attribute.format <= VertexFormat::float32x4;
                matched |= (unsignedFormat &&
                            (type == "uint" || type == "uvec2" || type == "uvec3" || type == "uvec4")) ||
                           (floatFormat &&
                            (type == "float" || type == "vec2" || type == "vec3" || type == "vec4"));
            }
        if (!matched) return invalid("vertex input has no matching pipeline attribute location/scalar type");
    }
    return Status::success();
}

Status validate_interfaces(ShaderStage stage, std::span<const ShaderInterfaceVariable> interfaces) {
    if ((stage != ShaderStage::vertex && stage != ShaderStage::fragment && stage != ShaderStage::compute) ||
        (stage == ShaderStage::compute && !interfaces.empty()))
        return invalid("located stage IO requires a supported graphics stage");
    std::set<std::tuple<ShaderStage, ShaderInterfaceDirection, std::uint32_t>> keys;
    for (const auto& value : interfaces) {
        if (value.stage != stage ||
            (value.direction != ShaderInterfaceDirection::input &&
             value.direction != ShaderInterfaceDirection::output) ||
            value.type.empty() || !keys.emplace(value.stage, value.direction, value.location).second) {
            return invalid("shader interface stage/direction/type/location is invalid or duplicated");
        }
    }
    return Status::success();
}

Status validate_workgroup(ShaderStage stage, Extent3D defaults,
                          const WorkgroupSpecializationIds& ids,
                          std::span<const ShaderSpecializationConstant> constants) {
    if (stage != ShaderStage::compute) {
        return std::any_of(ids.begin(), ids.end(), [](const auto& id) { return id.has_value(); })
            ? invalid("workgroup specialization requires a compute shader") : Status::success();
    }
    const std::array dimensions{defaults.width, defaults.height, defaults.depth};
    for (std::size_t axis = 0; axis < ids.size(); ++axis) {
        if (dimensions[axis] == 0) return invalid("workgroup dimensions must be positive");
        if (!ids[axis]) continue;
        const auto found = std::find_if(constants.begin(), constants.end(), [&](const auto& constant) {
            return constant.id == *ids[axis];
        });
        if (found == constants.end() || !positive_integer(found->type, found->defaultValueBits) ||
            found->defaultValueBits != dimensions[axis]) {
            return invalid("workgroup axis references a missing/nonpositive/noninteger or inconsistent specialization default");
        }
    }
    return Status::success();
}

Result<Extent3D> resolve_workgroup(const ShaderDesc& shader,
                                  std::span<const SpecializationValue> values) {
    auto status = validate_workgroup(shader.stage, shader.requiredWorkgroupSize,
                                     shader.workgroupSpecializationIds, shader.specializationConstants);
    if (!status.ok()) return status;
    std::array dimensions{shader.requiredWorkgroupSize.width,
                          shader.requiredWorkgroupSize.height, shader.requiredWorkgroupSize.depth};
    for (std::size_t axis = 0; axis < dimensions.size(); ++axis) {
        if (!shader.workgroupSpecializationIds[axis]) continue;
        const auto id = *shader.workgroupSpecializationIds[axis];
        const auto constant = std::find_if(shader.specializationConstants.begin(),
            shader.specializationConstants.end(), [&](const auto& value) { return value.id == id; });
        const auto replacement = std::find_if(values.begin(), values.end(),
            [&](const auto& value) { return value.id == id; });
        if (replacement == values.end()) continue;
        if (replacement->type != constant->type || !positive_integer(replacement->type, replacement->valueBits))
            return invalid("workgroup specialization override must be a positive matching integer");
        dimensions[axis] = replacement->valueBits;
    }
    return Extent3D{dimensions[0], dimensions[1], dimensions[2]};
}

} // namespace truffle::rhi::detail

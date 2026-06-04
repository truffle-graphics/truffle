#include "truffle/assets/assets.hpp"

#include <algorithm>

namespace truffle::assets {

std::size_t attribute_format_size(AttributeFormat format) noexcept {
    switch (format) {
    case AttributeFormat::Float32:
        return 4;
    case AttributeFormat::Float32x2:
        return 8;
    case AttributeFormat::Float32x3:
        return 12;
    case AttributeFormat::Float32x4:
        return 16;
    case AttributeFormat::Float32x4x4:
        return 64;
    case AttributeFormat::UInt32:
    case AttributeFormat::SInt32:
        return 4;
    case AttributeFormat::UInt16:
        return 2;
    case AttributeFormat::UInt8x4Norm:
        return 4;
    }
    return 0;
}

bool has_attribute(const GeometryStreamDesc& stream,
                   AttributeSemantic semantic) noexcept {
    return std::any_of(
        stream.attributes.begin(), stream.attributes.end(),
        [semantic](const AttributeDesc& attribute) {
            return attribute.semantic == semantic;
        });
}

std::size_t minimum_stride(const GeometryStreamDesc& stream) noexcept {
    std::size_t stride = 0;
    for (const auto& attribute : stream.attributes) {
        stride = std::max(stride,
                          attribute.offset +
                              attribute_format_size(attribute.format));
    }
    return stride;
}

bool requires_attribute(const MaterialAssetDesc& material,
                        AttributeSemantic semantic) noexcept {
    const auto explicitRequirement = std::any_of(
        material.requiredAttributes.begin(), material.requiredAttributes.end(),
        [semantic](AttributeSemantic required) {
            return required == semantic;
        });
    if (explicitRequirement) {
        return true;
    }

    return std::any_of(
        material.operations.begin(), material.operations.end(),
        [semantic](const MaterialOperationDesc& operation) {
            return std::any_of(
                operation.inputs.begin(), operation.inputs.end(),
                [semantic](const MaterialSignalRef& input) {
                    return input.source == MaterialValueSource::Attribute &&
                           input.attribute == semantic;
                });
        });
}

bool has_operation(const MaterialAssetDesc& material,
                   std::string_view name) noexcept {
    return std::any_of(
        material.operations.begin(), material.operations.end(),
        [name](const MaterialOperationDesc& operation) {
            return operation.name == name;
        });
}

} // namespace truffle::assets

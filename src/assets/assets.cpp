#include "truffle/assets/assets.hpp"

#include <algorithm>

namespace truffle::assets {

namespace {

bool contains_attribute(const std::vector<AttributeSemantic>& attributes,
                        AttributeSemantic semantic) noexcept {
    return std::any_of(
        attributes.begin(), attributes.end(),
        [semantic](AttributeSemantic attribute) {
            return attribute == semantic;
        });
}

void append_unique_attribute(std::vector<AttributeSemantic>& attributes,
                             AttributeSemantic semantic) {
    if (!contains_attribute(attributes, semantic)) {
        attributes.push_back(semantic);
    }
}

template <typename T>
core::Status add_asset(std::unordered_map<AssetId, T, AssetIdHash>& assets,
                       T asset) {
    if (!asset.id.valid()) {
        return core::Status::failure(
            core::StatusCode::invalid_argument,
            "Assets: asset id must be valid");
    }
    const auto [_, inserted] = assets.try_emplace(asset.id, std::move(asset));
    if (!inserted) {
        return core::Status::failure(
            core::StatusCode::invalid_state,
            "Assets: asset id is already registered");
    }
    return core::Status::success();
}

template <typename T>
const T* find_asset(const std::unordered_map<AssetId, T, AssetIdHash>& assets,
                    AssetId id) noexcept {
    const auto it = assets.find(id);
    return it == assets.end() ? nullptr : &it->second;
}

} // namespace

core::Status AssetCatalog::add_geometry_stream(GeometryStreamDesc stream) {
    return add_asset(geometryStreams_, std::move(stream));
}

core::Status AssetCatalog::add_texture(TextureAssetDesc texture) {
    return add_asset(textures_, std::move(texture));
}

core::Status AssetCatalog::add_material(MaterialAssetDesc material) {
    return add_asset(materials_, std::move(material));
}

core::Status AssetCatalog::add_mesh(MeshAssetDesc mesh) {
    return add_asset(meshes_, std::move(mesh));
}

const GeometryStreamDesc* AssetCatalog::geometry_stream(
    AssetId id) const noexcept {
    return find_asset(geometryStreams_, id);
}

const TextureAssetDesc* AssetCatalog::texture(AssetId id) const noexcept {
    return find_asset(textures_, id);
}

const MaterialAssetDesc* AssetCatalog::material(AssetId id) const noexcept {
    return find_asset(materials_, id);
}

const MeshAssetDesc* AssetCatalog::mesh(AssetId id) const noexcept {
    return find_asset(meshes_, id);
}

core::Status AssetCatalog::validate_mesh_material(AssetId meshId) const {
    const auto* foundMesh = mesh(meshId);
    if (foundMesh == nullptr) {
        return core::Status::failure(
            core::StatusCode::unavailable,
            "Assets: mesh asset is not registered");
    }
    if (!foundMesh->material.valid()) {
        return core::Status::failure(
            core::StatusCode::invalid_argument,
            "Assets: mesh does not reference a valid material");
    }

    const auto* foundMaterial = material(foundMesh->material);
    if (foundMaterial == nullptr) {
        return core::Status::failure(
            core::StatusCode::unavailable,
            "Assets: material asset is not registered");
    }

    return validate_material_requirements(*foundMaterial, *foundMesh);
}

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

std::vector<AttributeSemantic> collect_required_attributes(
    const MaterialAssetDesc& material) {
    std::vector<AttributeSemantic> attributes;
    attributes.reserve(material.requiredAttributes.size());

    for (const auto semantic : material.requiredAttributes) {
        append_unique_attribute(attributes, semantic);
    }

    for (const auto& operation : material.operations) {
        for (const auto& input : operation.inputs) {
            if (input.source == MaterialValueSource::Attribute) {
                append_unique_attribute(attributes, input.attribute);
            }
        }
    }

    return attributes;
}

bool provides_attribute(const MeshAssetDesc& mesh,
                        AttributeSemantic semantic) noexcept {
    return std::any_of(
        mesh.streams.begin(), mesh.streams.end(),
        [semantic](const GeometryStreamDesc& stream) {
            return has_attribute(stream, semantic);
        });
}

core::Status validate_material_requirements(
    const MaterialAssetDesc& material,
    const MeshAssetDesc& mesh) {
    for (const auto semantic : collect_required_attributes(material)) {
        if (!provides_attribute(mesh, semantic)) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Assets: mesh is missing an attribute required by material");
        }
    }

    return core::Status::success();
}

} // namespace truffle::assets

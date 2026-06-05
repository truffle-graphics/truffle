#pragma once

#include "truffle/core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace truffle::assets {

struct AssetId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    [[nodiscard]] constexpr bool operator==(const AssetId&) const noexcept =
        default;
};

struct AssetIdHash {
    [[nodiscard]] std::size_t operator()(AssetId id) const noexcept {
        return static_cast<std::size_t>(id.value);
    }
};

enum class AssetKind {
    Unknown,
    Mesh,
    GeometryStream,
    Material,
    Texture,
};

enum class DataResidency {
    CpuMemory,
    GpuResident,
    External,
};

enum class GeometryStreamRole {
    Vertex,
    Index,
    Instance,
    Custom,
};

enum class AttributeSemantic {
    Position,
    Transform,
    LocalTransform,
    Color,
    Normal,
    TexCoord,
    ParentIndex,
    Scale,
    Radius,
    Velocity,
    Intensity,
    Confidence,
    Classification,
    Custom,
};

enum class AttributeFormat {
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
    Float32x4x4,
    UInt32,
    SInt32,
    UInt16,
    UInt8x4Norm,
};

struct AttributeDesc {
    AttributeSemantic semantic = AttributeSemantic::Custom;
    AttributeFormat   format   = AttributeFormat::Float32;
    std::uint32_t     stream   = 0;
    std::size_t       offset   = 0;
    std::size_t       stride   = 0;
    std::string       name;
};

struct GeometryStreamDesc {
    AssetId            id;
    std::string        name;
    GeometryStreamRole role      = GeometryStreamRole::Vertex;
    DataResidency      residency = DataResidency::CpuMemory;
    std::uint32_t      elementCount = 0;
    std::size_t        byteSize     = 0;
    std::vector<AttributeDesc> attributes;
};

enum class TextureFormat {
    Unknown,
    Rgba8Unorm,
    Bgra8Unorm,
    Depth32Float,
};

struct TextureAssetDesc {
    AssetId       id;
    std::string   name;
    TextureFormat format = TextureFormat::Unknown;
    std::uint32_t width  = 1;
    std::uint32_t height = 1;
};

enum class MaterialParameterKind {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Texture,
};

struct MaterialParameterDesc {
    std::string           name;
    MaterialParameterKind kind = MaterialParameterKind::Float;
    std::uint32_t         binding = 0;
};

enum class MaterialValueSource {
    Attribute,
    Parameter,
    Constant,
    Texture,
    Builtin,
    Custom,
};

enum class MaterialValueShape {
    Unknown,
    Scalar,
    Float2,
    Float3,
    Float4,
    Matrix4,
    Texture2D,
};

struct MaterialSignalRef {
    MaterialValueSource source = MaterialValueSource::Attribute;
    AttributeSemantic   attribute = AttributeSemantic::Custom;
    std::string         name;
    MaterialValueShape  shape = MaterialValueShape::Unknown;
};

enum class MaterialOperationKind {
    PassThrough,
    Add,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
    Clamp,
    Normalize,
    Dot,
    Mix,
    Remap,
    Step,
    SmoothStep,
    SampleTexture,
    Custom,
};

struct MaterialOperationDesc {
    std::string name;
    MaterialOperationKind kind = MaterialOperationKind::Custom;
    std::vector<MaterialSignalRef> inputs;
    MaterialSignalRef output;
    std::vector<std::string> featureTags;
};

struct MaterialAssetDesc {
    AssetId id;
    std::string name;
    std::vector<AttributeSemantic> requiredAttributes;
    std::vector<MaterialParameterDesc> parameters;
    std::vector<MaterialOperationDesc> operations;
    std::vector<std::string> featureTags;
};

struct MeshAssetDesc {
    AssetId id;
    std::string name;
    AssetId material;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount  = 0;
    std::vector<GeometryStreamDesc> streams;
};

enum class AssetValidationIssueKind {
    MissingMesh,
    InvalidMaterialReference,
    MissingMaterial,
    MissingAttribute,
};

struct AssetValidationIssue {
    AssetValidationIssueKind kind = AssetValidationIssueKind::MissingAttribute;
    AssetId mesh;
    AssetId material;
    AttributeSemantic attribute = AttributeSemantic::Custom;
    std::string message;
};

struct AssetValidationReport {
    std::vector<AssetValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }
};

struct AssetCatalogStats {
    std::size_t geometryStreamCount = 0;
    std::size_t textureCount = 0;
    std::size_t materialCount = 0;
    std::size_t meshCount = 0;
    std::size_t cpuGeometryStreamCount = 0;
    std::size_t gpuResidentGeometryStreamCount = 0;
    std::size_t externalGeometryStreamCount = 0;
    std::size_t totalGeometryElements = 0;
    std::size_t totalGeometryBytes = 0;
    AssetId largestGeometryStream;
    std::size_t largestGeometryStreamBytes = 0;
};

class AssetCatalog {
public:
    [[nodiscard]] core::Status add_geometry_stream(GeometryStreamDesc stream);
    [[nodiscard]] core::Status add_texture(TextureAssetDesc texture);
    [[nodiscard]] core::Status add_material(MaterialAssetDesc material);
    [[nodiscard]] core::Status add_mesh(MeshAssetDesc mesh);

    [[nodiscard]] const GeometryStreamDesc* geometry_stream(
        AssetId id) const noexcept;
    [[nodiscard]] const TextureAssetDesc* texture(AssetId id) const noexcept;
    [[nodiscard]] const MaterialAssetDesc* material(AssetId id) const noexcept;
    [[nodiscard]] const MeshAssetDesc* mesh(AssetId id) const noexcept;

    [[nodiscard]] core::Status validate_mesh_material(
        AssetId meshId) const;
    [[nodiscard]] AssetValidationReport validate_mesh_material_report(
        AssetId meshId) const;
    [[nodiscard]] AssetValidationReport validate_all_mesh_materials() const;
    [[nodiscard]] AssetCatalogStats stats() const noexcept;

    [[nodiscard]] std::size_t geometry_stream_count() const noexcept {
        return geometryStreams_.size();
    }
    [[nodiscard]] std::size_t texture_count() const noexcept {
        return textures_.size();
    }
    [[nodiscard]] std::size_t material_count() const noexcept {
        return materials_.size();
    }
    [[nodiscard]] std::size_t mesh_count() const noexcept {
        return meshes_.size();
    }

private:
    std::unordered_map<AssetId, GeometryStreamDesc, AssetIdHash>
        geometryStreams_;
    std::unordered_map<AssetId, TextureAssetDesc, AssetIdHash> textures_;
    std::unordered_map<AssetId, MaterialAssetDesc, AssetIdHash> materials_;
    std::unordered_map<AssetId, MeshAssetDesc, AssetIdHash> meshes_;
};

[[nodiscard]] std::size_t attribute_format_size(AttributeFormat format) noexcept;
[[nodiscard]] bool has_attribute(const GeometryStreamDesc& stream,
                                 AttributeSemantic semantic) noexcept;
[[nodiscard]] std::size_t minimum_stride(
    const GeometryStreamDesc& stream) noexcept;
[[nodiscard]] bool requires_attribute(const MaterialAssetDesc& material,
                                      AttributeSemantic semantic) noexcept;
[[nodiscard]] bool has_operation(const MaterialAssetDesc& material,
                                 std::string_view name) noexcept;
[[nodiscard]] std::vector<AttributeSemantic> collect_required_attributes(
    const MaterialAssetDesc& material);
[[nodiscard]] bool provides_attribute(const MeshAssetDesc& mesh,
                                      AttributeSemantic semantic) noexcept;
[[nodiscard]] core::Status validate_material_requirements(
    const MaterialAssetDesc& material,
    const MeshAssetDesc& mesh);

} // namespace truffle::assets

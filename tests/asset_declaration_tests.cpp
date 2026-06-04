#include "test_support.hpp"
#include "truffle/assets/assets.hpp"

int main() {
    using namespace truffle::assets;

    GeometryStreamDesc detections;
    detections.id = AssetId{7};
    detections.name = "dense_detections";
    detections.role = GeometryStreamRole::Instance;
    detections.residency = DataResidency::External;
    detections.elementCount = 1'000'000;
    detections.byteSize = 32'000'000;
    detections.attributes.push_back({
        .semantic = AttributeSemantic::Position,
        .format = AttributeFormat::Float32x3,
        .stream = 0,
        .offset = 0,
        .stride = 32,
        .name = "position",
    });
    detections.attributes.push_back({
        .semantic = AttributeSemantic::Confidence,
        .format = AttributeFormat::Float32,
        .stream = 0,
        .offset = 12,
        .stride = 32,
        .name = "confidence",
    });
    detections.attributes.push_back({
        .semantic = AttributeSemantic::Color,
        .format = AttributeFormat::Float32x4,
        .stream = 0,
        .offset = 16,
        .stride = 32,
        .name = "color",
    });

    TRUFFLE_CHECK(detections.id.valid());
    TRUFFLE_CHECK(attribute_format_size(AttributeFormat::Float32x3) == 12);
    TRUFFLE_CHECK(attribute_format_size(AttributeFormat::Float32x4x4) == 64);
    TRUFFLE_CHECK(has_attribute(detections, AttributeSemantic::Position));
    TRUFFLE_CHECK(has_attribute(detections, AttributeSemantic::Confidence));
    TRUFFLE_CHECK(!has_attribute(detections, AttributeSemantic::Velocity));
    TRUFFLE_CHECK(minimum_stride(detections) == 32);

    MaterialAssetDesc material;
    material.id = AssetId{9};
    material.name = "confidence_color";
    material.requiredAttributes = {
        AttributeSemantic::Position,
        AttributeSemantic::Confidence,
    };
    material.parameters.push_back({
        .name = "gain",
        .kind = MaterialParameterKind::Float,
        .binding = 0,
    });
    material.operations.push_back({
        .name = "confidence_gain",
        .kind = MaterialOperationKind::Multiply,
        .inputs = {
            {
                .source = MaterialValueSource::Attribute,
                .attribute = AttributeSemantic::Confidence,
                .name = "confidence",
                .shape = MaterialValueShape::Scalar,
            },
            {
                .source = MaterialValueSource::Parameter,
                .attribute = AttributeSemantic::Custom,
                .name = "gain",
                .shape = MaterialValueShape::Scalar,
            },
        },
        .output = {
            .source = MaterialValueSource::Custom,
            .attribute = AttributeSemantic::Custom,
            .name = "weightedConfidence",
            .shape = MaterialValueShape::Scalar,
        },
        .featureTags = {"confidence-ramp"},
    });
    material.featureTags.push_back("confidence-ramp");

    MeshAssetDesc mesh;
    mesh.id = AssetId{11};
    mesh.name = "detection_points";
    mesh.material = material.id;
    mesh.vertexCount = 1;
    mesh.streams.push_back(detections);

    TRUFFLE_CHECK(material.id.valid());
    TRUFFLE_CHECK(material.requiredAttributes.size() == 2);
    TRUFFLE_CHECK(requires_attribute(material, AttributeSemantic::Position));
    TRUFFLE_CHECK(requires_attribute(material, AttributeSemantic::Confidence));
    TRUFFLE_CHECK(!requires_attribute(material, AttributeSemantic::Velocity));
    TRUFFLE_CHECK(has_operation(material, "confidence_gain"));
    TRUFFLE_CHECK(!has_operation(material, "velocity_color"));
    const auto requiredAttributes = collect_required_attributes(material);
    TRUFFLE_CHECK(requiredAttributes.size() == 2);
    TRUFFLE_CHECK(mesh.streams.size() == 1);
    TRUFFLE_CHECK(mesh.streams.front().elementCount == 1'000'000);
    TRUFFLE_CHECK(provides_attribute(mesh, AttributeSemantic::Position));
    TRUFFLE_CHECK(provides_attribute(mesh, AttributeSemantic::Confidence));
    TRUFFLE_CHECK(!provides_attribute(mesh, AttributeSemantic::Velocity));
    TRUFFLE_CHECK(validate_material_requirements(material, mesh).ok());

    auto velocityMaterial = material;
    velocityMaterial.operations.push_back({
        .name = "velocity_color",
        .kind = MaterialOperationKind::Normalize,
        .inputs = {
            {
                .source = MaterialValueSource::Attribute,
                .attribute = AttributeSemantic::Velocity,
                .name = "velocity",
                .shape = MaterialValueShape::Float3,
            },
        },
        .output = {
            .source = MaterialValueSource::Custom,
            .attribute = AttributeSemantic::Custom,
            .name = "velocityDirection",
            .shape = MaterialValueShape::Float3,
        },
    });
    const auto missingAttributeStatus =
        validate_material_requirements(velocityMaterial, mesh);
    TRUFFLE_CHECK(!missingAttributeStatus.ok());
    TRUFFLE_CHECK(missingAttributeStatus.code ==
                  truffle::core::StatusCode::invalid_argument);

    TextureAssetDesc texture;
    texture.id = AssetId{13};
    texture.name = "classification_lut";
    texture.format = TextureFormat::Rgba8Unorm;
    texture.width = 256;
    texture.height = 1;

    TRUFFLE_CHECK(texture.id.valid());
    TRUFFLE_CHECK(texture.width == 256);

    return 0;
}

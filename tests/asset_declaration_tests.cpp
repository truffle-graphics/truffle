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
    velocityMaterial.requiredAttributes.push_back(AttributeSemantic::Intensity);
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

    AssetCatalog emptyCatalog;
    const auto emptyStats = emptyCatalog.stats();
    TRUFFLE_CHECK(emptyStats.geometryStreamCount == 0);
    TRUFFLE_CHECK(emptyStats.totalGeometryBytes == 0);
    TRUFFLE_CHECK(!emptyStats.largestGeometryStream.valid());

    AssetCatalog catalog;
    TRUFFLE_CHECK(catalog.add_geometry_stream(detections).ok());
    TRUFFLE_CHECK(catalog.add_material(material).ok());
    TRUFFLE_CHECK(catalog.add_texture(texture).ok());
    TRUFFLE_CHECK(catalog.add_mesh(mesh).ok());
    TRUFFLE_CHECK(catalog.geometry_stream_count() == 1);
    TRUFFLE_CHECK(catalog.material_count() == 1);
    TRUFFLE_CHECK(catalog.texture_count() == 1);
    TRUFFLE_CHECK(catalog.mesh_count() == 1);
    TRUFFLE_CHECK(catalog.geometry_stream(detections.id) != nullptr);
    TRUFFLE_CHECK(catalog.material(material.id) != nullptr);
    TRUFFLE_CHECK(catalog.texture(texture.id) != nullptr);
    TRUFFLE_CHECK(catalog.mesh(mesh.id) != nullptr);
    TRUFFLE_CHECK(catalog.validate_mesh_material(mesh.id).ok());
    TRUFFLE_CHECK(catalog.validate_mesh_material_report(mesh.id).ok());
    const auto initialStats = catalog.stats();
    TRUFFLE_CHECK(initialStats.geometryStreamCount == 1);
    TRUFFLE_CHECK(initialStats.textureCount == 1);
    TRUFFLE_CHECK(initialStats.materialCount == 1);
    TRUFFLE_CHECK(initialStats.meshCount == 1);
    TRUFFLE_CHECK(initialStats.externalGeometryStreamCount == 1);
    TRUFFLE_CHECK(initialStats.cpuGeometryStreamCount == 0);
    TRUFFLE_CHECK(initialStats.gpuResidentGeometryStreamCount == 0);
    TRUFFLE_CHECK(initialStats.totalGeometryElements ==
                  detections.elementCount);
    TRUFFLE_CHECK(initialStats.totalGeometryBytes == detections.byteSize);
    TRUFFLE_CHECK(initialStats.largestGeometryStream == detections.id);
    TRUFFLE_CHECK(initialStats.largestGeometryStreamBytes ==
                  detections.byteSize);

    const auto duplicateStatus = catalog.add_mesh(mesh);
    TRUFFLE_CHECK(!duplicateStatus.ok());
    TRUFFLE_CHECK(duplicateStatus.code ==
                  truffle::core::StatusCode::invalid_state);

    MeshAssetDesc invalidMesh;
    invalidMesh.name = "invalid";
    const auto invalidMeshStatus = catalog.add_mesh(invalidMesh);
    TRUFFLE_CHECK(!invalidMeshStatus.ok());
    TRUFFLE_CHECK(invalidMeshStatus.code ==
                  truffle::core::StatusCode::invalid_argument);

    auto missingMaterialMesh = mesh;
    missingMaterialMesh.id = AssetId{17};
    missingMaterialMesh.material = AssetId{19};
    TRUFFLE_CHECK(catalog.add_mesh(missingMaterialMesh).ok());
    const auto missingMaterialStatus =
        catalog.validate_mesh_material(missingMaterialMesh.id);
    TRUFFLE_CHECK(!missingMaterialStatus.ok());
    TRUFFLE_CHECK(missingMaterialStatus.code ==
                  truffle::core::StatusCode::unavailable);
    const auto missingMaterialReport =
        catalog.validate_mesh_material_report(missingMaterialMesh.id);
    TRUFFLE_CHECK(!missingMaterialReport.ok());
    TRUFFLE_CHECK(missingMaterialReport.issues.size() == 1);
    TRUFFLE_CHECK(missingMaterialReport.issues.front().kind ==
                  AssetValidationIssueKind::MissingMaterial);

    auto missingAttributeMesh = mesh;
    missingAttributeMesh.id = AssetId{21};
    missingAttributeMesh.material = velocityMaterial.id;
    velocityMaterial.id = AssetId{23};
    TRUFFLE_CHECK(catalog.add_material(velocityMaterial).ok());
    missingAttributeMesh.material = velocityMaterial.id;
    TRUFFLE_CHECK(catalog.add_mesh(missingAttributeMesh).ok());
    const auto missingCatalogAttributeStatus =
        catalog.validate_mesh_material(missingAttributeMesh.id);
    TRUFFLE_CHECK(!missingCatalogAttributeStatus.ok());
    TRUFFLE_CHECK(missingCatalogAttributeStatus.code ==
                  truffle::core::StatusCode::invalid_argument);
    const auto missingAttributeReport =
        catalog.validate_mesh_material_report(missingAttributeMesh.id);
    TRUFFLE_CHECK(!missingAttributeReport.ok());
    TRUFFLE_CHECK(missingAttributeReport.issues.size() == 2);
    bool foundMissingIntensity = false;
    bool foundMissingVelocity = false;
    for (const auto& issue : missingAttributeReport.issues) {
        TRUFFLE_CHECK(issue.kind == AssetValidationIssueKind::MissingAttribute);
        foundMissingIntensity =
            foundMissingIntensity ||
            issue.attribute == AttributeSemantic::Intensity;
        foundMissingVelocity =
            foundMissingVelocity ||
            issue.attribute == AttributeSemantic::Velocity;
    }
    TRUFFLE_CHECK(foundMissingIntensity);
    TRUFFLE_CHECK(foundMissingVelocity);

    auto invalidMaterialReferenceMesh = mesh;
    invalidMaterialReferenceMesh.id = AssetId{25};
    invalidMaterialReferenceMesh.material = AssetId{};
    TRUFFLE_CHECK(catalog.add_mesh(invalidMaterialReferenceMesh).ok());
    const auto invalidMaterialReferenceReport =
        catalog.validate_mesh_material_report(invalidMaterialReferenceMesh.id);
    TRUFFLE_CHECK(!invalidMaterialReferenceReport.ok());
    TRUFFLE_CHECK(invalidMaterialReferenceReport.issues.size() == 1);
    TRUFFLE_CHECK(invalidMaterialReferenceReport.issues.front().kind ==
                  AssetValidationIssueKind::InvalidMaterialReference);

    const auto missingMeshReport =
        catalog.validate_mesh_material_report(AssetId{99});
    TRUFFLE_CHECK(!missingMeshReport.ok());
    TRUFFLE_CHECK(missingMeshReport.issues.front().kind ==
                  AssetValidationIssueKind::MissingMesh);

    const auto allReport = catalog.validate_all_mesh_materials();
    TRUFFLE_CHECK(!allReport.ok());
    TRUFFLE_CHECK(allReport.issues.size() == 4);

    AssetCatalog cleanCatalog;
    TRUFFLE_CHECK(cleanCatalog.add_material(material).ok());
    TRUFFLE_CHECK(cleanCatalog.add_mesh(mesh).ok());
    TRUFFLE_CHECK(cleanCatalog.validate_all_mesh_materials().ok());

    return 0;
}

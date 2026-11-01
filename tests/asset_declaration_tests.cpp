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

    TFL_CHECK(detections.id.valid());
    TFL_CHECK(attribute_format_size(AttributeFormat::Float32x3) == 12);
    TFL_CHECK(attribute_format_size(AttributeFormat::Float32x4x4) == 64);
    TFL_CHECK(has_attribute(detections, AttributeSemantic::Position));
    TFL_CHECK(has_attribute(detections, AttributeSemantic::Confidence));
    TFL_CHECK(!has_attribute(detections, AttributeSemantic::Velocity));
    TFL_CHECK(minimum_stride(detections) == 32);

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

    TFL_CHECK(material.id.valid());
    TFL_CHECK(material.requiredAttributes.size() == 2);
    TFL_CHECK(requires_attribute(material, AttributeSemantic::Position));
    TFL_CHECK(requires_attribute(material, AttributeSemantic::Confidence));
    TFL_CHECK(!requires_attribute(material, AttributeSemantic::Velocity));
    TFL_CHECK(has_operation(material, "confidence_gain"));
    TFL_CHECK(!has_operation(material, "velocity_color"));
    const auto requiredAttributes = collect_required_attributes(material);
    TFL_CHECK(requiredAttributes.size() == 2);
    TFL_CHECK(mesh.streams.size() == 1);
    TFL_CHECK(mesh.streams.front().elementCount == 1'000'000);
    TFL_CHECK(provides_attribute(mesh, AttributeSemantic::Position));
    TFL_CHECK(provides_attribute(mesh, AttributeSemantic::Confidence));
    TFL_CHECK(!provides_attribute(mesh, AttributeSemantic::Velocity));
    TFL_CHECK(validate_material_requirements(material, mesh).ok());

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
    TFL_CHECK(!missingAttributeStatus.ok());
    TFL_CHECK(missingAttributeStatus.code ==
                  truffle::base::StatusCode::invalid_argument);

    TextureAssetDesc texture;
    texture.id = AssetId{13};
    texture.name = "classification_lut";
    texture.format = TextureFormat::Rgba8Unorm;
    texture.width = 256;
    texture.height = 1;

    TFL_CHECK(texture.id.valid());
    TFL_CHECK(texture.width == 256);

    AssetCatalog emptyCatalog;
    const auto emptyStats = emptyCatalog.stats();
    TFL_CHECK(emptyStats.geometryStreamCount == 0);
    TFL_CHECK(emptyStats.totalGeometryBytes == 0);
    TFL_CHECK(!emptyStats.largestGeometryStream.valid());

    AssetCatalog catalog;
    TFL_CHECK(catalog.add_geometry_stream(detections).ok());
    TFL_CHECK(catalog.add_material(material).ok());
    TFL_CHECK(catalog.add_texture(texture).ok());
    TFL_CHECK(catalog.add_mesh(mesh).ok());
    TFL_CHECK(catalog.geometry_stream_count() == 1);
    TFL_CHECK(catalog.material_count() == 1);
    TFL_CHECK(catalog.texture_count() == 1);
    TFL_CHECK(catalog.mesh_count() == 1);
    TFL_CHECK(catalog.geometry_stream(detections.id) != nullptr);
    TFL_CHECK(catalog.material(material.id) != nullptr);
    TFL_CHECK(catalog.texture(texture.id) != nullptr);
    TFL_CHECK(catalog.mesh(mesh.id) != nullptr);
    TFL_CHECK(catalog.validate_mesh_material(mesh.id).ok());
    TFL_CHECK(catalog.validate_mesh_material_report(mesh.id).ok());
    const auto initialStats = catalog.stats();
    TFL_CHECK(initialStats.geometryStreamCount == 1);
    TFL_CHECK(initialStats.textureCount == 1);
    TFL_CHECK(initialStats.materialCount == 1);
    TFL_CHECK(initialStats.meshCount == 1);
    TFL_CHECK(initialStats.externalGeometryStreamCount == 1);
    TFL_CHECK(initialStats.cpuGeometryStreamCount == 0);
    TFL_CHECK(initialStats.gpuResidentGeometryStreamCount == 0);
    TFL_CHECK(initialStats.totalGeometryElements ==
                  detections.elementCount);
    TFL_CHECK(initialStats.totalGeometryBytes == detections.byteSize);
    TFL_CHECK(initialStats.largestGeometryStream == detections.id);
    TFL_CHECK(initialStats.largestGeometryStreamBytes ==
                  detections.byteSize);

    AssetGroupDesc denseGroup;
    denseGroup.id = AssetId{31};
    denseGroup.name = "dense-detections";
    denseGroup.tags = {"dense", "lidar"};
    denseGroup.geometryStreams.push_back(detections.id);
    denseGroup.textures.push_back(texture.id);
    denseGroup.materials.push_back(material.id);
    denseGroup.meshes.push_back(mesh.id);
    TFL_CHECK(catalog.add_group(denseGroup).ok());
    TFL_CHECK(catalog.group_count() == 1);
    TFL_CHECK(catalog.group(denseGroup.id) != nullptr);
    TFL_CHECK(catalog.validate_group(denseGroup.id).ok());
    const auto groupStats = catalog.group_stats(denseGroup.id);
    TFL_CHECK(groupStats.ok());
    TFL_CHECK(groupStats.value().group == denseGroup.id);
    TFL_CHECK(groupStats.value().name == "dense-detections");
    TFL_CHECK(groupStats.value().tags.size() == 2);
    TFL_CHECK(groupStats.value().stats.geometryStreamCount == 1);
    TFL_CHECK(groupStats.value().stats.textureCount == 1);
    TFL_CHECK(groupStats.value().stats.materialCount == 1);
    TFL_CHECK(groupStats.value().stats.meshCount == 1);
    TFL_CHECK(groupStats.value().stats.externalGeometryStreamCount == 1);
    TFL_CHECK(groupStats.value().stats.totalGeometryBytes ==
                  detections.byteSize);
    const auto allGroupIds = catalog.group_ids();
    TFL_CHECK(allGroupIds.size() == 1);
    const auto denseGroupIds = catalog.group_ids_with_tag("dense");
    TFL_CHECK(denseGroupIds.size() == 1);
    TFL_CHECK(denseGroupIds.front() == denseGroup.id);
    TFL_CHECK(catalog.group_ids_with_tag("radar").empty());

    AssetGroupDesc invalidGroup;
    invalidGroup.name = "invalid";
    const auto invalidGroupStatus = catalog.add_group(invalidGroup);
    TFL_CHECK(!invalidGroupStatus.ok());
    TFL_CHECK(invalidGroupStatus.code ==
                  truffle::base::StatusCode::invalid_argument);

    const auto duplicateStatus = catalog.add_mesh(mesh);
    TFL_CHECK(!duplicateStatus.ok());
    TFL_CHECK(duplicateStatus.code ==
                  truffle::base::StatusCode::invalid_state);

    MeshAssetDesc invalidMesh;
    invalidMesh.name = "invalid";
    const auto invalidMeshStatus = catalog.add_mesh(invalidMesh);
    TFL_CHECK(!invalidMeshStatus.ok());
    TFL_CHECK(invalidMeshStatus.code ==
                  truffle::base::StatusCode::invalid_argument);

    auto missingMaterialMesh = mesh;
    missingMaterialMesh.id = AssetId{17};
    missingMaterialMesh.material = AssetId{19};
    TFL_CHECK(catalog.add_mesh(missingMaterialMesh).ok());
    const auto missingMaterialStatus =
        catalog.validate_mesh_material(missingMaterialMesh.id);
    TFL_CHECK(!missingMaterialStatus.ok());
    TFL_CHECK(missingMaterialStatus.code ==
                  truffle::base::StatusCode::unavailable);
    const auto missingMaterialReport =
        catalog.validate_mesh_material_report(missingMaterialMesh.id);
    TFL_CHECK(!missingMaterialReport.ok());
    TFL_CHECK(missingMaterialReport.issues.size() == 1);
    TFL_CHECK(missingMaterialReport.issues.front().kind ==
                  AssetValidationIssueKind::MissingMaterial);

    auto missingAttributeMesh = mesh;
    missingAttributeMesh.id = AssetId{21};
    missingAttributeMesh.material = velocityMaterial.id;
    velocityMaterial.id = AssetId{23};
    TFL_CHECK(catalog.add_material(velocityMaterial).ok());
    missingAttributeMesh.material = velocityMaterial.id;
    TFL_CHECK(catalog.add_mesh(missingAttributeMesh).ok());
    const auto missingCatalogAttributeStatus =
        catalog.validate_mesh_material(missingAttributeMesh.id);
    TFL_CHECK(!missingCatalogAttributeStatus.ok());
    TFL_CHECK(missingCatalogAttributeStatus.code ==
                  truffle::base::StatusCode::invalid_argument);
    const auto missingAttributeReport =
        catalog.validate_mesh_material_report(missingAttributeMesh.id);
    TFL_CHECK(!missingAttributeReport.ok());
    TFL_CHECK(missingAttributeReport.issues.size() == 2);
    bool foundMissingIntensity = false;
    bool foundMissingVelocity = false;
    for (const auto& issue : missingAttributeReport.issues) {
        TFL_CHECK(issue.kind == AssetValidationIssueKind::MissingAttribute);
        foundMissingIntensity =
            foundMissingIntensity ||
            issue.attribute == AttributeSemantic::Intensity;
        foundMissingVelocity =
            foundMissingVelocity ||
            issue.attribute == AttributeSemantic::Velocity;
    }
    TFL_CHECK(foundMissingIntensity);
    TFL_CHECK(foundMissingVelocity);

    auto invalidMaterialReferenceMesh = mesh;
    invalidMaterialReferenceMesh.id = AssetId{25};
    invalidMaterialReferenceMesh.material = AssetId{};
    TFL_CHECK(catalog.add_mesh(invalidMaterialReferenceMesh).ok());
    const auto invalidMaterialReferenceReport =
        catalog.validate_mesh_material_report(invalidMaterialReferenceMesh.id);
    TFL_CHECK(!invalidMaterialReferenceReport.ok());
    TFL_CHECK(invalidMaterialReferenceReport.issues.size() == 1);
    TFL_CHECK(invalidMaterialReferenceReport.issues.front().kind ==
                  AssetValidationIssueKind::InvalidMaterialReference);

    const auto missingMeshReport =
        catalog.validate_mesh_material_report(AssetId{99});
    TFL_CHECK(!missingMeshReport.ok());
    TFL_CHECK(missingMeshReport.issues.front().kind ==
                  AssetValidationIssueKind::MissingMesh);

    const auto allReport = catalog.validate_all_mesh_materials();
    TFL_CHECK(!allReport.ok());
    TFL_CHECK(allReport.issues.size() == 4);

    AssetGroupDesc brokenGroup;
    brokenGroup.id = AssetId{33};
    brokenGroup.name = "broken-detections";
    brokenGroup.tags = {"dense", "broken"};
    brokenGroup.geometryStreams.push_back(AssetId{101});
    brokenGroup.textures.push_back(AssetId{102});
    brokenGroup.materials.push_back(AssetId{103});
    brokenGroup.meshes.push_back(missingAttributeMesh.id);
    brokenGroup.meshes.push_back(AssetId{104});
    TFL_CHECK(catalog.add_group(brokenGroup).ok());
    const auto brokenGroupStats = catalog.group_stats(brokenGroup.id);
    TFL_CHECK(brokenGroupStats.ok());
    TFL_CHECK(brokenGroupStats.value().stats.geometryStreamCount == 0);
    TFL_CHECK(brokenGroupStats.value().stats.textureCount == 0);
    TFL_CHECK(brokenGroupStats.value().stats.materialCount == 0);
    TFL_CHECK(brokenGroupStats.value().stats.meshCount == 1);

    const auto brokenGroupReport = catalog.validate_group(brokenGroup.id);
    TFL_CHECK(!brokenGroupReport.ok());
    TFL_CHECK(brokenGroupReport.issues.size() == 6);
    bool foundMissingStream = false;
    bool foundMissingTexture = false;
    bool foundMissingGroupMaterial = false;
    bool foundGroupMissingMesh = false;
    std::size_t groupedMissingAttributes = 0;
    for (const auto& issue : brokenGroupReport.issues) {
        TFL_CHECK(issue.group == brokenGroup.id);
        foundMissingStream =
            foundMissingStream ||
            issue.kind == AssetValidationIssueKind::MissingGeometryStream;
        foundMissingTexture =
            foundMissingTexture ||
            issue.kind == AssetValidationIssueKind::MissingTexture;
        foundMissingGroupMaterial =
            foundMissingGroupMaterial ||
            (issue.kind == AssetValidationIssueKind::MissingMaterial &&
             issue.asset == AssetId{103});
        foundGroupMissingMesh =
            foundGroupMissingMesh ||
            issue.kind == AssetValidationIssueKind::MissingMesh;
        if (issue.kind == AssetValidationIssueKind::MissingAttribute) {
            ++groupedMissingAttributes;
        }
    }
    TFL_CHECK(foundMissingStream);
    TFL_CHECK(foundMissingTexture);
    TFL_CHECK(foundMissingGroupMaterial);
    TFL_CHECK(foundGroupMissingMesh);
    TFL_CHECK(groupedMissingAttributes == 2);

    const auto missingGroupReport = catalog.validate_group(AssetId{199});
    TFL_CHECK(!missingGroupReport.ok());
    TFL_CHECK(missingGroupReport.issues.front().kind ==
                  AssetValidationIssueKind::MissingGroup);
    const auto missingGroupStats = catalog.group_stats(AssetId{199});
    TFL_CHECK(!missingGroupStats.ok());
    TFL_CHECK(missingGroupStats.status().code ==
                  truffle::base::StatusCode::unavailable);

    AssetCatalog cleanCatalog;
    TFL_CHECK(cleanCatalog.add_material(material).ok());
    TFL_CHECK(cleanCatalog.add_mesh(mesh).ok());
    TFL_CHECK(cleanCatalog.validate_all_mesh_materials().ok());

    return 0;
}

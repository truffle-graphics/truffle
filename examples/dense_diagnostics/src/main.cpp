#include "truffle/asset_render/asset_render.hpp"
#include "truffle/diagnostics/diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {

truffle::assets::GeometryStreamDesc make_dense_stream(
    truffle::assets::AssetId id,
    std::string name,
    std::uint32_t elementCount,
    std::size_t byteSize) {
    using namespace truffle::assets;

    GeometryStreamDesc stream;
    stream.id = id;
    stream.name = std::move(name);
    stream.role = GeometryStreamRole::Instance;
    stream.residency = DataResidency::External;
    stream.elementCount = elementCount;
    stream.byteSize = byteSize;
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Position,
        .format = AttributeFormat::Float32x3,
        .stream = 0,
        .offset = 0,
        .stride = 32,
        .name = "position",
    });
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Confidence,
        .format = AttributeFormat::Float32,
        .stream = 0,
        .offset = 12,
        .stride = 32,
        .name = "confidence",
    });
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Classification,
        .format = AttributeFormat::UInt32,
        .stream = 0,
        .offset = 16,
        .stride = 32,
        .name = "classification",
    });
    return stream;
}

int run_example(bool smoke) {
    using namespace truffle;

    assets::AssetCatalog catalog;

    auto lidarStream =
        make_dense_stream(assets::AssetId{100}, "lidar-points", 1'000'000,
                          32'000'000);
    auto radarStream =
        make_dense_stream(assets::AssetId{200}, "radar-detections", 250'000,
                          8'000'000);

    assets::MaterialAssetDesc detectionMaterial;
    detectionMaterial.id = assets::AssetId{10};
    detectionMaterial.name = "confidence-classification";
    detectionMaterial.requiredAttributes = {
        assets::AttributeSemantic::Position,
        assets::AttributeSemantic::Confidence,
        assets::AttributeSemantic::Classification,
    };

    assets::MeshAssetDesc lidarMesh;
    lidarMesh.id = assets::AssetId{101};
    lidarMesh.name = "lidar-point-cloud";
    lidarMesh.material = detectionMaterial.id;
    lidarMesh.vertexCount = 1;
    lidarMesh.streams.push_back(lidarStream);

    assets::MeshAssetDesc radarMesh;
    radarMesh.id = assets::AssetId{201};
    radarMesh.name = "radar-detection-cloud";
    radarMesh.material = detectionMaterial.id;
    radarMesh.vertexCount = 1;
    radarMesh.streams.push_back(radarStream);

    if (!catalog.add_geometry_stream(lidarStream).ok()) return 1;
    if (!catalog.add_geometry_stream(radarStream).ok()) return 2;
    if (!catalog.add_material(detectionMaterial).ok()) return 3;
    if (!catalog.add_mesh(lidarMesh).ok()) return 4;
    if (!catalog.add_mesh(radarMesh).ok()) return 5;

    assets::AssetGroupDesc lidarGroup;
    lidarGroup.id = assets::AssetId{1000};
    lidarGroup.name = "lidar-detections";
    lidarGroup.tags = {"dense", "lidar"};
    lidarGroup.geometryStreams.push_back(lidarStream.id);
    lidarGroup.materials.push_back(detectionMaterial.id);
    lidarGroup.meshes.push_back(lidarMesh.id);

    assets::AssetGroupDesc radarGroup;
    radarGroup.id = assets::AssetId{2000};
    radarGroup.name = "radar-detections";
    radarGroup.tags = {"dense", "radar"};
    radarGroup.geometryStreams.push_back(radarStream.id);
    radarGroup.materials.push_back(detectionMaterial.id);
    radarGroup.meshes.push_back(radarMesh.id);

    if (!catalog.add_group(lidarGroup).ok()) return 6;
    if (!catalog.add_group(radarGroup).ok()) return 7;

    asset_render::RenderBatchPlanOptions renderPlanOptions;
    renderPlanOptions.bindingModel = render::BindingModel::Interleaved;
    renderPlanOptions.channelMappings = {
        {assets::AttributeSemantic::Position,
         render::ChannelKind::CustomFloat4},
        {assets::AttributeSemantic::Confidence,
         render::ChannelKind::CustomFloat4},
        {assets::AttributeSemantic::Classification,
         render::ChannelKind::CustomFloat4},
    };
    auto lidarPlanResult = asset_render::plan_catalog_render_batch(
        catalog, lidarMesh.id, renderPlanOptions);
    if (!lidarPlanResult.ok()) return 8;
    const auto& lidarPlan = lidarPlanResult.value();
    if (lidarPlan.bindings.size() != 1 ||
        lidarPlan.bindings.front().byteSize != lidarStream.byteSize) {
        return 9;
    }
    const auto& lidarBatch = lidarPlan.batch;

    diagnostics::DebugOverlayLayer overlay;
    overlay.name = "dense-sensor-overlay";
    overlay.boxes.push_back({
        .metadata = {
            .name = "lidar-volume",
            .group = lidarGroup.id,
            .tags = {"lidar", "bounds"},
        },
        .min = {-50.0F, -20.0F, -5.0F},
        .max = {50.0F, 20.0F, 10.0F},
        .color = {0.2F, 0.8F, 1.0F, 1.0F},
    });
    overlay.labels.push_back({
        .metadata = {
            .name = "lidar-label",
            .group = lidarGroup.id,
            .tags = {"lidar", "label"},
        },
        .position = {0.0F, 22.0F, 0.0F},
        .text = "lidar: 1M grouped detections",
        .color = {1.0F, 1.0F, 1.0F, 1.0F},
    });
    overlay.pickTargets.push_back({
        .metadata = {
            .name = "lidar-picker",
            .group = lidarGroup.id,
            .tags = {"lidar", "pick"},
        },
        .targetId = 1000,
        .min = {-50.0F, -20.0F, -5.0F},
        .max = {50.0F, 20.0F, 10.0F},
    });

    diagnostics::DiagnosticsBundleOptions options;
    options.assetCatalog = &catalog;
    options.assetCatalogOptions.name = "dense-sensor-catalog";
    options.assetCatalogOptions.groupTags.push_back("lidar");
    options.renderBatches.push_back({
        .batch = &lidarBatch,
        .options = {
            .name = "lidar-point-cloud-batch",
        },
    });
    options.debugOverlay = &overlay;
    options.debugOverlayOptions.tags.push_back("lidar");
    options.renderBatchBudget.maxInstances = 2'000'000;

    auto bundleResult = diagnostics::collect_diagnostics_bundle(options);
    if (!bundleResult.ok()) return 10;

    const auto& bundle = bundleResult.value();
    if (!bundle.hasAssetCatalog || bundle.assetCatalog.groups.size() != 1) {
        return 11;
    }
    if (bundle.assetCatalog.groups.front().stats.totalGeometryBytes !=
        lidarStream.byteSize) {
        return 12;
    }
    if (bundle.renderBatches.size() != 1 ||
        bundle.renderBatches.front().instanceCount != lidarStream.elementCount) {
        return 13;
    }
    if (!bundle.hasDebugOverlay || bundle.debugOverlay.primitiveCount != 3) {
        return 14;
    }
    if (!bundle.findings.empty()) {
        return 15;
    }

    if (!smoke) {
        std::cout << diagnostics::format_diagnostics_bundle(bundle) << '\n';
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke =
        argc > 1 && std::string{argv[1]} == std::string{"--smoke"};
    return run_example(smoke);
}

#include "test_support.hpp"
#include "truffle/diagnostics/diagnostics.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestBuffer final : public truffle::rhi::IBuffer {
public:
    explicit TestBuffer(truffle::rhi::BufferDesc desc) : desc_(std::move(desc)) {}

    [[nodiscard]] const truffle::rhi::BufferDesc& desc() const noexcept override {
        return desc_;
    }

private:
    truffle::rhi::BufferDesc desc_;
};

} // namespace

int main() {
    using namespace truffle;

    assets::GeometryStreamDesc assetStream;
    assetStream.id = assets::AssetId{7};
    assetStream.name = "dense_detections";
    assetStream.role = assets::GeometryStreamRole::Instance;
    assetStream.residency = assets::DataResidency::External;
    assetStream.elementCount = 1'000'000;
    assetStream.byteSize = 32'000'000;
    assetStream.attributes.push_back({
        .semantic = assets::AttributeSemantic::Position,
        .format = assets::AttributeFormat::Float32x3,
        .stream = 0,
        .offset = 0,
        .stride = 32,
        .name = "position",
    });

    assets::MaterialAssetDesc assetMaterial;
    assetMaterial.id = assets::AssetId{9};
    assetMaterial.name = "detection_material";
    assetMaterial.requiredAttributes.push_back(
        assets::AttributeSemantic::Position);

    assets::MeshAssetDesc assetMesh;
    assetMesh.id = assets::AssetId{11};
    assetMesh.name = "detection_points";
    assetMesh.material = assetMaterial.id;
    assetMesh.vertexCount = 1;
    assetMesh.streams.push_back(assetStream);

    assets::AssetCatalog assetCatalog;
    TRUFFLE_CHECK(assetCatalog.add_geometry_stream(assetStream).ok());
    TRUFFLE_CHECK(assetCatalog.add_material(assetMaterial).ok());
    TRUFFLE_CHECK(assetCatalog.add_mesh(assetMesh).ok());

    auto missingMaterialMesh = assetMesh;
    missingMaterialMesh.id = assets::AssetId{13};
    missingMaterialMesh.material = assets::AssetId{99};
    TRUFFLE_CHECK(assetCatalog.add_mesh(missingMaterialMesh).ok());

    assets::AssetGroupDesc assetGroup;
    assetGroup.id = assets::AssetId{15};
    assetGroup.name = "lidar-detections";
    assetGroup.tags = {"dense", "lidar"};
    assetGroup.geometryStreams.push_back(assetStream.id);
    assetGroup.materials.push_back(assetMaterial.id);
    assetGroup.meshes.push_back(assetMesh.id);
    assetGroup.meshes.push_back(missingMaterialMesh.id);
    TRUFFLE_CHECK(assetCatalog.add_group(assetGroup).ok());

    const auto assetSummary = diagnostics::summarize_asset_catalog(
        assetCatalog, {
            .name = "dense-asset-catalog",
            .includeGroups = true,
        });
    TRUFFLE_CHECK(assetSummary.name == "dense-asset-catalog");
    TRUFFLE_CHECK(assetSummary.stats.geometryStreamCount == 1);
    TRUFFLE_CHECK(assetSummary.stats.externalGeometryStreamCount == 1);
    TRUFFLE_CHECK(assetSummary.stats.totalGeometryBytes == 32'000'000);
    TRUFFLE_CHECK(assetSummary.validation.issues.size() == 1);
    TRUFFLE_CHECK(assetSummary.groups.size() == 1);
    TRUFFLE_CHECK(assetSummary.groups.front().group == assetGroup.id);
    TRUFFLE_CHECK(assetSummary.groups.front().validation.issues.size() == 1);
    const auto assetReport =
        diagnostics::format_asset_catalog_summary(assetSummary);
    TRUFFLE_CHECK(assetReport.find("name=dense-asset-catalog") !=
                  std::string::npos);
    TRUFFLE_CHECK(assetReport.find("bytes=32000000") != std::string::npos);
    TRUFFLE_CHECK(assetReport.find("kind=MissingMaterial") !=
                  std::string::npos);
    TRUFFLE_CHECK(assetReport.find("attribute=Custom") != std::string::npos);
    TRUFFLE_CHECK(assetReport.find("asset_group id=15 name=lidar-detections") !=
                  std::string::npos);
    TRUFFLE_CHECK(assetReport.find("tag lidar") != std::string::npos);

    diagnostics::DebugOverlayLayer overlay;
    overlay.name = "tool-overlay";
    overlay.lines.push_back({
        .metadata = {
            .name = "velocity-ray",
            .group = assetGroup.id,
            .tags = {"dense", "lidar"},
        },
        .begin = {-1.0F, 0.0F, 0.0F},
        .end = {1.0F, 0.0F, 0.0F},
        .color = {0.0F, 1.0F, 0.0F, 1.0F},
    });
    overlay.boxes.push_back({
        .metadata = {
            .name = "sensor-bounds",
            .group = assetGroup.id,
            .tags = {"lidar"},
        },
        .min = {-2.0F, -3.0F, -4.0F},
        .max = {10.0F, 3.0F, 4.0F},
        .color = {1.0F, 1.0F, 0.0F, 1.0F},
    });
    overlay.points.push_back({
        .metadata = {
            .name = "radar-hit",
            .tags = {"radar"},
        },
        .position = {100.0F, 100.0F, 100.0F},
        .radius = 2.0F,
        .color = {1.0F, 0.0F, 0.0F, 1.0F},
    });
    overlay.labels.push_back({
        .metadata = {
            .name = "sensor-label",
            .group = assetGroup.id,
            .tags = {"lidar"},
        },
        .position = {0.0F, 2.0F, 0.0F},
        .text = "lidar",
        .color = {1.0F, 1.0F, 1.0F, 1.0F},
    });
    overlay.pickTargets.push_back({
        .metadata = {
            .name = "sensor-pick-target",
            .group = assetGroup.id,
            .tags = {"lidar"},
        },
        .targetId = 42,
        .min = {-2.0F, -3.0F, -4.0F},
        .max = {10.0F, 3.0F, 4.0F},
    });

    const auto overlaySummary = diagnostics::summarize_debug_overlay(overlay);
    TRUFFLE_CHECK(overlaySummary.primitiveCount == 5);
    TRUFFLE_CHECK(overlaySummary.pointCount == 1);
    TRUFFLE_CHECK(overlaySummary.bounds.valid);
    TRUFFLE_CHECK(overlaySummary.bounds.max.x == 100.0F);
    TRUFFLE_CHECK(overlaySummary.groups.size() == 1);

    const auto lidarOverlaySummary = diagnostics::summarize_debug_overlay(
        overlay, {
            .tags = {"lidar"},
        });
    TRUFFLE_CHECK(lidarOverlaySummary.primitiveCount == 4);
    TRUFFLE_CHECK(lidarOverlaySummary.pointCount == 0);
    TRUFFLE_CHECK(lidarOverlaySummary.bounds.max.x == 10.0F);
    TRUFFLE_CHECK(lidarOverlaySummary.tags.size() == 2);
    const auto overlayReport =
        diagnostics::format_debug_overlay_summary(lidarOverlaySummary);
    TRUFFLE_CHECK(overlayReport.find("DebugOverlay name=tool-overlay") !=
                  std::string::npos);
    TRUFFLE_CHECK(overlayReport.find("primitives=4") != std::string::npos);
    TRUFFLE_CHECK(overlayReport.find("pickTargets=1") != std::string::npos);
    TRUFFLE_CHECK(overlayReport.find("group 15") != std::string::npos);

    TestBuffer instanceBuffer{{
        .size = 32'000'000,
        .usage = rhi::BufferUsage::vertex,
        .debugName = "dense_detection_instances",
    }};

    render::RenderBatch batch;
    batch.instanceCount = 1'000'000;
    batch.vertexCount = 1;
    batch.material = 42;
    batch.layout.bindingModel = render::BindingModel::Interleaved;
    batch.layout.channels.push_back({
        .kind = render::ChannelKind::Transform,
        .binding = 0,
        .offset = 0,
        .stride = 80,
    });
    batch.layout.channels.push_back({
        .kind = render::ChannelKind::Color,
        .binding = 0,
        .offset = 64,
        .stride = 80,
    });
    batch.bindings[0] = {
        .buffer = &instanceBuffer,
        .offset = 0,
        .size = 32'000'000,
    };

    const auto batchSummary = diagnostics::summarize_render_batch(batch, {
        .name = "dense-detection-batch",
    });
    TRUFFLE_CHECK(batchSummary.name == "dense-detection-batch");
    TRUFFLE_CHECK(batchSummary.instanceCount == 1'000'000);
    TRUFFLE_CHECK(batchSummary.vertexCount == 1);
    TRUFFLE_CHECK(batchSummary.material == 42);
    TRUFFLE_CHECK(batchSummary.channels.size() == 2);
    TRUFFLE_CHECK(batchSummary.bindings.size() == 1);
    TRUFFLE_CHECK(batchSummary.totalBindingBytes == 32'000'000);
    TRUFFLE_CHECK(diagnostics::evaluate_render_batch_budget(
        batchSummary, {}).empty());
    const auto batchFindings = diagnostics::evaluate_render_batch_budget(
        batchSummary, {
            .maxInstances = 999'999,
            .maxChannels = 1,
            .maxBindingBytes = 1,
        });
    TRUFFLE_CHECK(batchFindings.size() == 3);
    TRUFFLE_CHECK(batchFindings.front().code == "render_batch.instances");

    const auto batchReport =
        diagnostics::format_render_batch_summary(batchSummary);
    TRUFFLE_CHECK(batchReport.find("name=dense-detection-batch") !=
                  std::string::npos);
    TRUFFLE_CHECK(batchReport.find("instances=1000000") != std::string::npos);
    TRUFFLE_CHECK(batchReport.find("bindingBytes=32000000") !=
                  std::string::npos);
    const auto batchFindingsReport =
        diagnostics::format_diagnostic_findings(batchFindings);
    TRUFFLE_CHECK(batchFindingsReport.find("DiagnosticFindings count=3") !=
                  std::string::npos);
    TRUFFLE_CHECK(batchFindingsReport.find("render_batch.binding_bytes") !=
                  std::string::npos);

    rhi::RenderPassDesc passDesc;
    passDesc.extent = {640, 480};

    render::FrameGraph graph;
    const auto nodeId = graph.add_node(std::make_unique<render::RenderPassNode>(
        passDesc, std::vector<render::RenderBatch>{batch}));
    TRUFFLE_CHECK(nodeId == 0);
    const auto presentNodeId = graph.add_node(
        std::make_unique<render::RenderPassNode>(
            true, std::vector<render::RenderBatch>{}));
    TRUFFLE_CHECK(presentNodeId == 1);
    TRUFFLE_CHECK(graph.add_dependency(nodeId, presentNodeId).ok());
    TRUFFLE_CHECK(graph.add_resource_usage(nodeId, {
        .resourceId = 99,
        .access = render::ResourceAccess::Write,
    }).ok());
    TRUFFLE_CHECK(graph.add_resource_usage(presentNodeId, {
        .resourceId = 99,
        .access = render::ResourceAccess::Read,
    }).ok());

    diagnostics::FrameGraphInspectionOptions options;
    options.nodeLabels.push_back({
        .id = nodeId,
        .name = "dense-detection-render",
    });
    options.nodeLabels.push_back({
        .id = presentNodeId,
        .name = "present",
    });
    options.resourceLabels.push_back({
        .resourceId = 99,
        .name = "dense-detection-buffer",
    });

    auto graphSummaryResult = diagnostics::summarize_frame_graph(graph, options);
    TRUFFLE_CHECK(graphSummaryResult.ok());
    const auto& graphSummary = graphSummaryResult.value();
    TRUFFLE_CHECK(graphSummary.nodes.size() == 2);
    TRUFFLE_CHECK(graphSummary.nodes.front().name == "dense-detection-render");
    TRUFFLE_CHECK(graphSummary.resourceLabels.size() == 1);
    TRUFFLE_CHECK(graphSummary.dependencies.size() == 1);
    TRUFFLE_CHECK(graphSummary.dependencies.front().before == nodeId);
    TRUFFLE_CHECK(graphSummary.dependencies.front().after == presentNodeId);
    TRUFFLE_CHECK(graphSummary.resourceUsages.size() == 2);
    TRUFFLE_CHECK(graphSummary.resourceUsages.front().resourceName ==
                  "dense-detection-buffer");
    TRUFFLE_CHECK(graphSummary.executionOrder.size() == 2);
    TRUFFLE_CHECK(graphSummary.renderNodeCount == 2);
    TRUFFLE_CHECK(graphSummary.renderBatchCount == 1);
    TRUFFLE_CHECK(graphSummary.totalInstanceCount == 1'000'000);
    TRUFFLE_CHECK(diagnostics::evaluate_frame_graph_budget(
        graphSummary, {}).empty());
    const auto graphFindings = diagnostics::evaluate_frame_graph_budget(
        graphSummary, {
            .maxRenderNodes = 1,
            .maxInstances = 999'999,
            .maxResourceUsages = 1,
        });
    TRUFFLE_CHECK(graphFindings.size() == 3);

    const auto graphReport =
        diagnostics::format_frame_graph_summary(graphSummary);
    TRUFFLE_CHECK(graphReport.find("name=dense-detection-render") !=
                  std::string::npos);
    TRUFFLE_CHECK(graphReport.find("name=dense-detection-buffer") !=
                  std::string::npos);
    TRUFFLE_CHECK(graphReport.find("dependency 0->1") != std::string::npos);
    TRUFFLE_CHECK(graphReport.find("access=Write") != std::string::npos);
    TRUFFLE_CHECK(graphReport.find("access=Read") != std::string::npos);
    TRUFFLE_CHECK(graphReport.find("instances=1000000") != std::string::npos);
    const auto graphFindingsReport =
        diagnostics::format_diagnostic_findings(graphFindings);
    TRUFFLE_CHECK(graphFindingsReport.find("frame_graph.render_nodes") !=
                  std::string::npos);
    TRUFFLE_CHECK(graphFindingsReport.find("frame_graph.resource_usages") !=
                  std::string::npos);

    const auto statsSummary = diagnostics::summarize_renderer_stats({
        .computeNodesExecuted = 1,
        .renderNodesExecuted = 2,
        .renderBatchesExecuted = 3,
        .presented = true,
    });
    TRUFFLE_CHECK(statsSummary.computeNodesExecuted == 1);
    TRUFFLE_CHECK(statsSummary.presented);
    const auto statsReport =
        diagnostics::format_renderer_stats_summary(statsSummary);
    TRUFFLE_CHECK(statsReport.find("presented=true") != std::string::npos);

    render::RendererFrameStats frameStats;
    frameStats.computeNodesExecuted = 1;
    frameStats.renderNodesExecuted = 2;
    frameStats.renderBatchesExecuted = 1;
    frameStats.presented = true;

    diagnostics::DiagnosticsBundleOptions bundleOptions;
    bundleOptions.assetCatalog = &assetCatalog;
    bundleOptions.assetCatalogOptions.name = "dense-asset-catalog";
    bundleOptions.assetCatalogOptions.groupTags.push_back("lidar");
    bundleOptions.renderBatches.push_back({
        .batch = &batch,
        .options = {
            .name = "dense-detection-batch",
        },
    });
    bundleOptions.frameGraph = &graph;
    bundleOptions.frameGraphOptions = options;
    bundleOptions.rendererStats = &frameStats;
    bundleOptions.debugOverlay = &overlay;
    bundleOptions.debugOverlayOptions.tags.push_back("lidar");
    bundleOptions.renderBatchBudget.maxInstances = 999'999;
    bundleOptions.frameGraphBudget.maxRenderNodes = 1;

    auto bundleResult =
        diagnostics::collect_diagnostics_bundle(bundleOptions);
    TRUFFLE_CHECK(bundleResult.ok());
    const auto& bundle = bundleResult.value();
    TRUFFLE_CHECK(bundle.hasAssetCatalog);
    TRUFFLE_CHECK(bundle.assetCatalog.validation.issues.size() == 1);
    TRUFFLE_CHECK(bundle.assetCatalog.groups.size() == 1);
    TRUFFLE_CHECK(bundle.assetCatalog.groups.front().name ==
                  "lidar-detections");
    TRUFFLE_CHECK(bundle.renderBatches.size() == 1);
    TRUFFLE_CHECK(bundle.hasFrameGraph);
    TRUFFLE_CHECK(bundle.hasRendererStats);
    TRUFFLE_CHECK(bundle.hasDebugOverlay);
    TRUFFLE_CHECK(bundle.debugOverlay.primitiveCount == 4);
    TRUFFLE_CHECK(bundle.findings.size() == 2);

    const auto bundleReport = diagnostics::format_diagnostics_bundle(bundle);
    TRUFFLE_CHECK(bundleReport.find("DiagnosticsBundle assetCatalog=true") !=
                  std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("AssetCatalog name=dense-asset-catalog") !=
                  std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("RenderBatch name=dense-detection-batch") !=
                  std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("FrameGraph nodes=2") != std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("RendererStats computeNodes=1") !=
                  std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("DebugOverlay name=tool-overlay") !=
                  std::string::npos);
    TRUFFLE_CHECK(bundleReport.find("DiagnosticFindings count=2") !=
                  std::string::npos);

    diagnostics::DiagnosticsBundleOptions invalidBundleOptions;
    invalidBundleOptions.renderBatches.push_back({});
    const auto invalidBundleResult =
        diagnostics::collect_diagnostics_bundle(invalidBundleOptions);
    TRUFFLE_CHECK(!invalidBundleResult.ok());
    TRUFFLE_CHECK(invalidBundleResult.status().code ==
                  core::StatusCode::invalid_argument);

    return 0;
}

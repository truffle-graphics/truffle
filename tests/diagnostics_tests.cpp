#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/diagnostics/diagnostics.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    TFL_CHECK(assetCatalog.add_geometry_stream(assetStream).ok());
    TFL_CHECK(assetCatalog.add_material(assetMaterial).ok());
    TFL_CHECK(assetCatalog.add_mesh(assetMesh).ok());

    auto missingMaterialMesh = assetMesh;
    missingMaterialMesh.id = assets::AssetId{13};
    missingMaterialMesh.material = assets::AssetId{99};
    TFL_CHECK(assetCatalog.add_mesh(missingMaterialMesh).ok());

    assets::AssetGroupDesc assetGroup;
    assetGroup.id = assets::AssetId{15};
    assetGroup.name = "lidar-detections";
    assetGroup.tags = {"dense", "lidar"};
    assetGroup.geometryStreams.push_back(assetStream.id);
    assetGroup.materials.push_back(assetMaterial.id);
    assetGroup.meshes.push_back(assetMesh.id);
    assetGroup.meshes.push_back(missingMaterialMesh.id);
    TFL_CHECK(assetCatalog.add_group(assetGroup).ok());

    const auto assetSummary = diagnostics::summarize_asset_catalog(
        assetCatalog, {
            .name = "dense-asset-catalog",
            .includeGroups = true,
        });
    TFL_CHECK(assetSummary.name == "dense-asset-catalog");
    TFL_CHECK(assetSummary.stats.geometryStreamCount == 1);
    TFL_CHECK(assetSummary.stats.externalGeometryStreamCount == 1);
    TFL_CHECK(assetSummary.stats.totalGeometryBytes == 32'000'000);
    TFL_CHECK(assetSummary.validation.issues.size() == 1);
    TFL_CHECK(assetSummary.groups.size() == 1);
    TFL_CHECK(assetSummary.groups.front().group == assetGroup.id);
    TFL_CHECK(assetSummary.groups.front().validation.issues.size() == 1);
    const auto assetReport =
        diagnostics::format_asset_catalog_summary(assetSummary);
    TFL_CHECK(assetReport.find("name=dense-asset-catalog") !=
                  std::string::npos);
    TFL_CHECK(assetReport.find("bytes=32000000") != std::string::npos);
    TFL_CHECK(assetReport.find("kind=MissingMaterial") !=
                  std::string::npos);
    TFL_CHECK(assetReport.find("attribute=Custom") != std::string::npos);
    TFL_CHECK(assetReport.find("asset_group id=15 name=lidar-detections") !=
                  std::string::npos);
    TFL_CHECK(assetReport.find("tag lidar") != std::string::npos);

    asset_render::RenderBatchPlanOptions assetRenderOptions;
    assetRenderOptions.channelMappings.push_back({
        assets::AttributeSemantic::Position,
        renderer::ChannelKind::CustomFloat4,
    });
    auto assetRenderPlanResult = asset_render::plan_render_batch(
        assetMesh, assetMaterial, assetRenderOptions);
    TFL_CHECK(assetRenderPlanResult.ok());
    const auto assetRenderSummary =
        diagnostics::summarize_asset_render_batch_plan(
            assetRenderPlanResult.value());
    TFL_CHECK(assetRenderSummary.mesh == assetMesh.id);
    TFL_CHECK(assetRenderSummary.material == assetMaterial.id);
    TFL_CHECK(assetRenderSummary.instanceCount ==
                  assetStream.elementCount);
    TFL_CHECK(assetRenderSummary.attributes.size() == 1);
    TFL_CHECK(assetRenderSummary.attributes.front().stream ==
                  assetStream.id);
    TFL_CHECK(assetRenderSummary.attributes.front().semantic ==
                  assets::AttributeSemantic::Position);
    TFL_CHECK(assetRenderSummary.attributes.front().channel ==
                  renderer::ChannelKind::CustomFloat4);
    TFL_CHECK(assetRenderSummary.bindings.size() == 1);
    TFL_CHECK(assetRenderSummary.bindings.front().byteSize ==
                  assetStream.byteSize);
    TFL_CHECK(assetRenderSummary.totalBindingBytes ==
                  assetStream.byteSize);
    const auto assetRenderReport =
        diagnostics::format_asset_render_batch_plan_summary(
            assetRenderSummary);
    TFL_CHECK(assetRenderReport.find("AssetRenderBatchPlan mesh=11") !=
                  std::string::npos);
    TFL_CHECK(assetRenderReport.find("semantic=Position") !=
                  std::string::npos);
    TFL_CHECK(assetRenderReport.find("channel=CustomFloat4") !=
                  std::string::npos);
    TFL_CHECK(assetRenderReport.find("planned_binding binding=0") !=
                  std::string::npos);

    asset_render::AssetGroupRenderPlan assetRenderGroupPlan;
    assetRenderGroupPlan.group = assets::AssetId{17};
    assetRenderGroupPlan.batches.push_back(assetRenderPlanResult.value());
    const auto assetRenderGroupSummary =
        diagnostics::summarize_asset_render_group_plan(assetRenderGroupPlan);
    TFL_CHECK(assetRenderGroupSummary.group == assetRenderGroupPlan.group);
    TFL_CHECK(assetRenderGroupSummary.batchCount == 1);
    TFL_CHECK(assetRenderGroupSummary.totalInstanceCount ==
                  assetStream.elementCount);
    TFL_CHECK(assetRenderGroupSummary.totalBindingBytes ==
                  assetStream.byteSize);
    const auto assetRenderGroupReport =
        diagnostics::format_asset_render_group_plan_summary(
            assetRenderGroupSummary);
    TFL_CHECK(assetRenderGroupReport.find("AssetRenderGroupPlan group=17") !=
                  std::string::npos);
    TFL_CHECK(assetRenderGroupReport.find("instances=1000000") !=
                  std::string::npos);

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
    TFL_CHECK(overlaySummary.primitiveCount == 5);
    TFL_CHECK(overlaySummary.pointCount == 1);
    TFL_CHECK(overlaySummary.bounds.valid);
    TFL_CHECK(overlaySummary.bounds.max.x == 100.0F);
    TFL_CHECK(overlaySummary.groups.size() == 1);

    const auto lidarOverlaySummary = diagnostics::summarize_debug_overlay(
        overlay, {
            .tags = {"lidar"},
        });
    TFL_CHECK(lidarOverlaySummary.primitiveCount == 4);
    TFL_CHECK(lidarOverlaySummary.pointCount == 0);
    TFL_CHECK(lidarOverlaySummary.bounds.max.x == 10.0F);
    TFL_CHECK(lidarOverlaySummary.tags.size() == 2);
    const auto overlayReport =
        diagnostics::format_debug_overlay_summary(lidarOverlaySummary);
    TFL_CHECK(overlayReport.find("DebugOverlay name=tool-overlay") !=
                  std::string::npos);
    TFL_CHECK(overlayReport.find("primitives=4") != std::string::npos);
    TFL_CHECK(overlayReport.find("pickTargets=1") != std::string::npos);
    TFL_CHECK(overlayReport.find("group 15") != std::string::npos);

    auto rhiContext = tests::make_null_context();
    auto instanceBufferResult = rhiContext.device.create_buffer({
        .size = 32'000'000,
        .usage = rhi::BufferUsage::vertex,
        .memory = rhi::MemoryDomain::upload,
        .debugName = "dense_detection_instances",
    });
    TFL_CHECK(instanceBufferResult.ok());
    auto instanceBuffer = std::move(instanceBufferResult).value();

    renderer::RenderBatch batch;
    batch.instanceCount = 1'000'000;
    batch.vertexCount = 1;
    batch.material = 42;
    batch.layout.bindingModel = renderer::BindingModel::Interleaved;
    batch.layout.channels.push_back({
        .kind = renderer::ChannelKind::Transform,
        .binding = 0,
        .offset = 0,
        .stride = 80,
    });
    batch.layout.channels.push_back({
        .kind = renderer::ChannelKind::Color,
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
    TFL_CHECK(batchSummary.name == "dense-detection-batch");
    TFL_CHECK(batchSummary.instanceCount == 1'000'000);
    TFL_CHECK(batchSummary.vertexCount == 1);
    TFL_CHECK(batchSummary.material == 42);
    TFL_CHECK(batchSummary.channels.size() == 2);
    TFL_CHECK(batchSummary.bindings.size() == 1);
    TFL_CHECK(batchSummary.totalBindingBytes == 32'000'000);
    TFL_CHECK(diagnostics::evaluate_render_batch_budget(
        batchSummary, {}).empty());
    const auto batchFindings = diagnostics::evaluate_render_batch_budget(
        batchSummary, {
            .maxInstances = 999'999,
            .maxChannels = 1,
            .maxBindingBytes = 1,
        });
    TFL_CHECK(batchFindings.size() == 3);
    TFL_CHECK(batchFindings.front().code == "render_batch.instances");

    const auto batchReport =
        diagnostics::format_render_batch_summary(batchSummary);
    TFL_CHECK(batchReport.find("name=dense-detection-batch") !=
                  std::string::npos);
    TFL_CHECK(batchReport.find("instances=1000000") != std::string::npos);
    TFL_CHECK(batchReport.find("bindingBytes=32000000") !=
                  std::string::npos);
    const auto batchFindingsReport =
        diagnostics::format_diagnostic_findings(batchFindings);
    TFL_CHECK(batchFindingsReport.find("DiagnosticFindings count=3") !=
                  std::string::npos);
    TFL_CHECK(batchFindingsReport.find("render_batch.binding_bytes") !=
                  std::string::npos);

    rhi::RenderPassDesc passDesc;
    passDesc.extent = {640, 480};

    renderer::FrameGraph graph;
    const auto nodeId = graph.add_node(std::make_unique<renderer::RenderPassNode>(
        passDesc, std::vector<renderer::RenderBatch>{batch}));
    TFL_CHECK(nodeId == 0);
    const auto presentNodeId = graph.add_node(
        std::make_unique<renderer::RenderPassNode>(
            true, std::vector<renderer::RenderBatch>{}));
    TFL_CHECK(presentNodeId == 1);
    TFL_CHECK(graph.add_dependency(nodeId, presentNodeId).ok());
    TFL_CHECK(graph.add_resource_usage(nodeId, {
        .resourceId = 99,
        .access = renderer::ResourceAccess::Write,
    }).ok());
    TFL_CHECK(graph.add_resource_usage(presentNodeId, {
        .resourceId = 99,
        .access = renderer::ResourceAccess::Read,
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
    TFL_CHECK(graphSummaryResult.ok());
    const auto& graphSummary = graphSummaryResult.value();
    TFL_CHECK(graphSummary.nodes.size() == 2);
    TFL_CHECK(graphSummary.nodes.front().name == "dense-detection-render");
    TFL_CHECK(graphSummary.resourceLabels.size() == 1);
    TFL_CHECK(graphSummary.dependencies.size() == 1);
    TFL_CHECK(graphSummary.dependencies.front().before == nodeId);
    TFL_CHECK(graphSummary.dependencies.front().after == presentNodeId);
    TFL_CHECK(graphSummary.resourceUsages.size() == 2);
    TFL_CHECK(graphSummary.resourceUsages.front().resourceName ==
                  "dense-detection-buffer");
    TFL_CHECK(graphSummary.executionOrder.size() == 2);
    TFL_CHECK(graphSummary.renderNodeCount == 2);
    TFL_CHECK(graphSummary.renderBatchCount == 1);
    TFL_CHECK(graphSummary.totalInstanceCount == 1'000'000);
    TFL_CHECK(diagnostics::evaluate_frame_graph_budget(
        graphSummary, {}).empty());
    const auto graphFindings = diagnostics::evaluate_frame_graph_budget(
        graphSummary, {
            .maxRenderNodes = 1,
            .maxInstances = 999'999,
            .maxResourceUsages = 1,
        });
    TFL_CHECK(graphFindings.size() == 3);

    const auto graphReport =
        diagnostics::format_frame_graph_summary(graphSummary);
    TFL_CHECK(graphReport.find("name=dense-detection-render") !=
                  std::string::npos);
    TFL_CHECK(graphReport.find("name=dense-detection-buffer") !=
                  std::string::npos);
    TFL_CHECK(graphReport.find("dependency 0->1") != std::string::npos);
    TFL_CHECK(graphReport.find("access=Write") != std::string::npos);
    TFL_CHECK(graphReport.find("access=Read") != std::string::npos);
    TFL_CHECK(graphReport.find("instances=1000000") != std::string::npos);
    const auto graphFindingsReport =
        diagnostics::format_diagnostic_findings(graphFindings);
    TFL_CHECK(graphFindingsReport.find("frame_graph.render_nodes") !=
                  std::string::npos);
    TFL_CHECK(graphFindingsReport.find("frame_graph.resource_usages") !=
                  std::string::npos);

    const auto statsSummary = diagnostics::summarize_renderer_stats({
        .computeNodesExecuted = 1,
        .renderNodesExecuted = 2,
        .renderBatchesExecuted = 3,
        .presented = true,
    });
    TFL_CHECK(statsSummary.computeNodesExecuted == 1);
    TFL_CHECK(statsSummary.presented);
    const auto statsReport =
        diagnostics::format_renderer_stats_summary(statsSummary);
    TFL_CHECK(statsReport.find("presented=true") != std::string::npos);

    renderer::RendererFrameStats frameStats;
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
    TFL_CHECK(bundleResult.ok());
    const auto& bundle = bundleResult.value();
    TFL_CHECK(bundle.hasAssetCatalog);
    TFL_CHECK(bundle.assetCatalog.validation.issues.size() == 1);
    TFL_CHECK(bundle.assetCatalog.groups.size() == 1);
    TFL_CHECK(bundle.assetCatalog.groups.front().name ==
                  "lidar-detections");
    TFL_CHECK(bundle.renderBatches.size() == 1);
    TFL_CHECK(bundle.hasFrameGraph);
    TFL_CHECK(bundle.hasRendererStats);
    TFL_CHECK(bundle.hasDebugOverlay);
    TFL_CHECK(bundle.debugOverlay.primitiveCount == 4);
    TFL_CHECK(bundle.findings.size() == 2);

    const auto bundleReport = diagnostics::format_diagnostics_bundle(bundle);
    TFL_CHECK(bundleReport.find("DiagnosticsBundle assetCatalog=true") !=
                  std::string::npos);
    TFL_CHECK(bundleReport.find("AssetCatalog name=dense-asset-catalog") !=
                  std::string::npos);
    TFL_CHECK(bundleReport.find("RenderBatch name=dense-detection-batch") !=
                  std::string::npos);
    TFL_CHECK(bundleReport.find("FrameGraph nodes=2") != std::string::npos);
    TFL_CHECK(bundleReport.find("RendererStats computeNodes=1") !=
                  std::string::npos);
    TFL_CHECK(bundleReport.find("DebugOverlay name=tool-overlay") !=
                  std::string::npos);
    TFL_CHECK(bundleReport.find("DiagnosticFindings count=2") !=
                  std::string::npos);

    diagnostics::DiagnosticsBundleOptions invalidBundleOptions;
    invalidBundleOptions.renderBatches.push_back({});
    const auto invalidBundleResult =
        diagnostics::collect_diagnostics_bundle(invalidBundleOptions);
    TFL_CHECK(!invalidBundleResult.ok());
    TFL_CHECK(invalidBundleResult.status().code ==
                  base::StatusCode::invalid_argument);

    return 0;
}

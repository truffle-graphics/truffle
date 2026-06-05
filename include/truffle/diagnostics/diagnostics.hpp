#pragma once

#include "truffle/assets/assets.hpp"
#include "truffle/core/status.hpp"
#include "truffle/render/frame_graph.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/render/renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace truffle::diagnostics {

struct ChannelSummary {
    render::ChannelKind kind = render::ChannelKind::Transform;
    std::uint32_t       binding = 0;
    std::uint32_t       offset  = 0;
    std::uint32_t       stride  = 0;
};

struct BufferBindingSummary {
    std::uint32_t binding = 0;
    std::size_t   offset  = 0;
    std::size_t   size    = 0;
};

struct BufferViewSummary {
    bool        present = false;
    std::size_t offset  = 0;
    std::size_t size    = 0;
};

struct RenderBatchInspectionOptions {
    std::string name;
};

struct RenderBatchSummary {
    std::string          name;
    std::uint32_t        instanceCount = 0;
    std::uint32_t        vertexCount   = 0;
    render::MaterialId   material      = 0;
    std::size_t          variantHash   = 0;
    render::DrawKind     drawKind      = render::DrawKind::Direct;
    std::size_t          layoutHash    = 0;
    render::BindingModel bindingModel  = render::BindingModel::Separate;
    std::vector<ChannelSummary> channels;
    std::vector<BufferBindingSummary> bindings;
    std::size_t totalBindingBytes = 0;
    BufferViewSummary indexBuffer;
    BufferViewSummary indirectBuffer;
    BufferViewSummary uniformBuffer;
};

struct FrameGraphNodeLabel {
    render::FrameGraph::NodeId id = 0;
    std::string name;
};

struct ResourceLabel {
    std::uint64_t resourceId = 0;
    std::string name;
};

struct FrameGraphInspectionOptions {
    std::vector<FrameGraphNodeLabel> nodeLabels;
    std::vector<ResourceLabel> resourceLabels;
};

struct FrameGraphNodeSummary {
    render::FrameGraph::NodeId id = 0;
    std::string name;
    render::FrameGraphNodeKind kind = render::FrameGraphNodeKind::Render;
    bool usesSwapchain = false;
    bool hasExplicitRenderPass = false;
    std::uint32_t renderBatchCount = 0;
    std::uint64_t instanceCount = 0;
};

struct FrameGraphDependencySummary {
    render::FrameGraph::NodeId before = 0;
    render::FrameGraph::NodeId after = 0;
};

struct FrameGraphResourceUsageSummary {
    render::FrameGraph::NodeId node = 0;
    std::uint64_t resourceId = 0;
    render::ResourceAccess access = render::ResourceAccess::Read;
    std::string resourceName;
};

struct FrameGraphSummary {
    std::vector<FrameGraphNodeSummary> nodes;
    std::vector<render::FrameGraph::NodeId> executionOrder;
    std::vector<FrameGraphDependencySummary> dependencies;
    std::vector<FrameGraphResourceUsageSummary> resourceUsages;
    std::vector<ResourceLabel> resourceLabels;
    std::uint32_t computeNodeCount = 0;
    std::uint32_t renderNodeCount = 0;
    std::uint32_t renderBatchCount = 0;
    std::uint64_t totalInstanceCount = 0;
};

struct RendererStatsSummary {
    std::uint32_t computeNodesExecuted = 0;
    std::uint32_t renderNodesExecuted = 0;
    std::uint32_t renderBatchesExecuted = 0;
    bool presented = false;
};

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct DiagnosticFinding {
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    std::string code;
    std::string message;
    std::uint64_t observed = 0;
    std::uint64_t limit = 0;
};

struct RenderBatchBudget {
    std::uint32_t maxInstances = 0;
    std::uint32_t maxVertexCount = 0;
    std::uint32_t maxChannels = 0;
    std::uint32_t maxBindings = 0;
    std::uint64_t maxBindingBytes = 0;
};

struct FrameGraphBudget {
    std::uint32_t maxComputeNodes = 0;
    std::uint32_t maxRenderNodes = 0;
    std::uint32_t maxRenderBatches = 0;
    std::uint64_t maxInstances = 0;
    std::uint32_t maxDependencies = 0;
    std::uint32_t maxResourceUsages = 0;
};

struct AssetCatalogInspectionOptions {
    std::string name;
    bool validateMeshMaterials = true;
};

struct AssetCatalogSummary {
    std::string name;
    assets::AssetCatalogStats stats;
    assets::AssetValidationReport validation;
};

struct RenderBatchInspectionTarget {
    const render::RenderBatch* batch = nullptr;
    RenderBatchInspectionOptions options;
};

struct DiagnosticsBundleOptions {
    const assets::AssetCatalog* assetCatalog = nullptr;
    AssetCatalogInspectionOptions assetCatalogOptions;
    std::vector<RenderBatchInspectionTarget> renderBatches;
    const render::FrameGraph* frameGraph = nullptr;
    FrameGraphInspectionOptions frameGraphOptions;
    const render::RendererFrameStats* rendererStats = nullptr;
    RenderBatchBudget renderBatchBudget;
    FrameGraphBudget frameGraphBudget;
};

struct DiagnosticsBundle {
    bool hasAssetCatalog = false;
    AssetCatalogSummary assetCatalog;
    std::vector<RenderBatchSummary> renderBatches;
    bool hasFrameGraph = false;
    FrameGraphSummary frameGraph;
    bool hasRendererStats = false;
    RendererStatsSummary rendererStats;
    std::vector<DiagnosticFinding> findings;
};

[[nodiscard]] AssetCatalogSummary summarize_asset_catalog(
    const assets::AssetCatalog& catalog);
[[nodiscard]] AssetCatalogSummary summarize_asset_catalog(
    const assets::AssetCatalog& catalog,
    const AssetCatalogInspectionOptions& options);
[[nodiscard]] RenderBatchSummary summarize_render_batch(
    const render::RenderBatch& batch);
[[nodiscard]] RenderBatchSummary summarize_render_batch(
    const render::RenderBatch& batch,
    const RenderBatchInspectionOptions& options);
[[nodiscard]] core::Result<FrameGraphSummary> summarize_frame_graph(
    const render::FrameGraph& graph);
[[nodiscard]] core::Result<FrameGraphSummary> summarize_frame_graph(
    const render::FrameGraph& graph,
    const FrameGraphInspectionOptions& options);
[[nodiscard]] RendererStatsSummary summarize_renderer_stats(
    const render::RendererFrameStats& stats) noexcept;
[[nodiscard]] std::vector<DiagnosticFinding> evaluate_render_batch_budget(
    const RenderBatchSummary& summary,
    const RenderBatchBudget& budget);
[[nodiscard]] std::vector<DiagnosticFinding> evaluate_frame_graph_budget(
    const FrameGraphSummary& summary,
    const FrameGraphBudget& budget);
[[nodiscard]] core::Result<DiagnosticsBundle> collect_diagnostics_bundle(
    const DiagnosticsBundleOptions& options);

[[nodiscard]] std::string format_asset_catalog_summary(
    const AssetCatalogSummary& summary);
[[nodiscard]] std::string format_render_batch_summary(
    const RenderBatchSummary& summary);
[[nodiscard]] std::string format_frame_graph_summary(
    const FrameGraphSummary& summary);
[[nodiscard]] std::string format_renderer_stats_summary(
    const RendererStatsSummary& summary);
[[nodiscard]] std::string format_diagnostic_findings(
    const std::vector<DiagnosticFinding>& findings);
[[nodiscard]] std::string format_diagnostics_bundle(
    const DiagnosticsBundle& bundle);

} // namespace truffle::diagnostics

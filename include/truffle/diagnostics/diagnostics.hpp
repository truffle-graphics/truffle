#pragma once

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

[[nodiscard]] std::string format_render_batch_summary(
    const RenderBatchSummary& summary);
[[nodiscard]] std::string format_frame_graph_summary(
    const FrameGraphSummary& summary);
[[nodiscard]] std::string format_renderer_stats_summary(
    const RendererStatsSummary& summary);

} // namespace truffle::diagnostics

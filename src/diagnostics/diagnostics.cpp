#include "truffle/diagnostics/diagnostics.hpp"

#include <sstream>

namespace truffle::diagnostics {

namespace {

const char* channel_kind_name(render::ChannelKind kind) noexcept {
    switch (kind) {
    case render::ChannelKind::Transform:
        return "Transform";
    case render::ChannelKind::LocalTransform:
        return "LocalTransform";
    case render::ChannelKind::Color:
        return "Color";
    case render::ChannelKind::Normal:
        return "Normal";
    case render::ChannelKind::TexCoord:
        return "TexCoord";
    case render::ChannelKind::ParentIndex:
        return "ParentIndex";
    case render::ChannelKind::CustomFloat4:
        return "CustomFloat4";
    }
    return "Unknown";
}

const char* draw_kind_name(render::DrawKind kind) noexcept {
    switch (kind) {
    case render::DrawKind::Direct:
        return "Direct";
    case render::DrawKind::Indexed:
        return "Indexed";
    case render::DrawKind::Indirect:
        return "Indirect";
    case render::DrawKind::IndirectIndexed:
        return "IndirectIndexed";
    }
    return "Unknown";
}

const char* node_kind_name(render::FrameGraphNodeKind kind) noexcept {
    switch (kind) {
    case render::FrameGraphNodeKind::Compute:
        return "Compute";
    case render::FrameGraphNodeKind::Render:
        return "Render";
    }
    return "Unknown";
}

const char* resource_access_name(render::ResourceAccess access) noexcept {
    switch (access) {
    case render::ResourceAccess::Read:
        return "Read";
    case render::ResourceAccess::Write:
        return "Write";
    }
    return "Unknown";
}

std::string find_node_label(const FrameGraphInspectionOptions& options,
                            render::FrameGraph::NodeId nodeId) {
    for (const auto& label : options.nodeLabels) {
        if (label.id == nodeId) {
            return label.name;
        }
    }
    return {};
}

std::string find_resource_label(const FrameGraphInspectionOptions& options,
                                std::uint64_t resourceId) {
    for (const auto& label : options.resourceLabels) {
        if (label.resourceId == resourceId) {
            return label.name;
        }
    }
    return {};
}

} // namespace

RenderBatchSummary summarize_render_batch(const render::RenderBatch& batch) {
    return summarize_render_batch(batch, {});
}

RenderBatchSummary summarize_render_batch(
    const render::RenderBatch& batch,
    const RenderBatchInspectionOptions& options) {
    RenderBatchSummary summary;
    summary.name          = options.name;
    summary.instanceCount = batch.instanceCount;
    summary.vertexCount   = batch.vertexCount;
    summary.material      = batch.material;
    summary.variantHash   = batch.variantHash;
    summary.drawKind      = batch.kind;
    summary.layoutHash    = batch.layout.hash();
    summary.bindingModel  = batch.layout.bindingModel;

    summary.channels.reserve(batch.layout.channels.size());
    for (const auto& channel : batch.layout.channels) {
        summary.channels.push_back({
            .kind    = channel.kind,
            .binding = channel.binding,
            .offset  = channel.offset,
            .stride  = channel.stride,
        });
    }

    for (std::uint32_t binding = 0; binding < render::RenderBatch::kMaxBindings;
         ++binding) {
        const auto& view = batch.bindings[binding];
        if (view.buffer == nullptr) {
            continue;
        }
        summary.bindings.push_back({
            .binding = binding,
            .offset  = view.offset,
            .size    = view.size,
        });
        summary.totalBindingBytes += view.size;
    }

    summary.indexBuffer = {
        .present = batch.indexBuffer.buffer != nullptr,
        .offset  = batch.indexBuffer.offset,
        .size    = batch.indexBuffer.size,
    };
    summary.indirectBuffer = {
        .present = batch.indirectBuffer.buffer != nullptr,
        .offset  = batch.indirectBuffer.offset,
        .size    = batch.indirectBuffer.size,
    };
    summary.uniformBuffer = {
        .present = batch.uniformBuffer.buffer != nullptr,
        .offset  = batch.uniformBuffer.offset,
        .size    = batch.uniformBuffer.size,
    };

    return summary;
}

core::Result<FrameGraphSummary> summarize_frame_graph(
    const render::FrameGraph& graph) {
    return summarize_frame_graph(graph, {});
}

core::Result<FrameGraphSummary> summarize_frame_graph(
    const render::FrameGraph& graph,
    const FrameGraphInspectionOptions& options) {
    auto executionOrder = graph.resolve_execution_order();
    if (!executionOrder.ok()) {
        return executionOrder.status();
    }

    FrameGraphSummary summary;
    summary.executionOrder = executionOrder.value();
    summary.resourceLabels = options.resourceLabels;

    const auto dependencies = graph.dependencies();
    summary.dependencies.reserve(dependencies.size());
    for (const auto& dependency : dependencies) {
        summary.dependencies.push_back({
            .before = dependency.first,
            .after  = dependency.second,
        });
    }

    const auto resourceUsages = graph.resource_usages();
    for (render::FrameGraph::NodeId nodeId = 0;
         nodeId < resourceUsages.size(); ++nodeId) {
        for (const auto& usage : resourceUsages[nodeId]) {
            summary.resourceUsages.push_back({
                .node = nodeId,
                .resourceId = usage.resourceId,
                .access = usage.access,
                .resourceName = find_resource_label(options, usage.resourceId),
            });
        }
    }

    const auto nodes = graph.nodes();
    summary.nodes.reserve(nodes.size());
    for (render::FrameGraph::NodeId nodeId = 0; nodeId < nodes.size(); ++nodeId) {
        const auto* node = graph.node(nodeId);
        if (node == nullptr) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Diagnostics: frame graph referenced an unknown node id");
        }

        FrameGraphNodeSummary nodeSummary;
        nodeSummary.id   = nodeId;
        nodeSummary.name = find_node_label(options, nodeId);
        nodeSummary.kind = node->kind();

        if (node->kind() == render::FrameGraphNodeKind::Compute) {
            ++summary.computeNodeCount;
        } else {
            const auto* renderNode =
                static_cast<const render::RenderPassNode*>(node);
            nodeSummary.usesSwapchain = renderNode->uses_swapchain();
            nodeSummary.hasExplicitRenderPass =
                renderNode->explicit_desc() != nullptr;
            nodeSummary.renderBatchCount =
                static_cast<std::uint32_t>(renderNode->batches().size());
            for (const auto& batch : renderNode->batches()) {
                nodeSummary.instanceCount += batch.instanceCount;
            }
            ++summary.renderNodeCount;
            summary.renderBatchCount += nodeSummary.renderBatchCount;
            summary.totalInstanceCount += nodeSummary.instanceCount;
        }

        summary.nodes.push_back(nodeSummary);
    }

    return summary;
}

RendererStatsSummary summarize_renderer_stats(
    const render::RendererFrameStats& stats) noexcept {
    return {
        .computeNodesExecuted = stats.computeNodesExecuted,
        .renderNodesExecuted  = stats.renderNodesExecuted,
        .renderBatchesExecuted = stats.renderBatchesExecuted,
        .presented = stats.presented,
    };
}

std::string format_render_batch_summary(const RenderBatchSummary& summary) {
    std::ostringstream out;
    out << "RenderBatch"
        << " name=" << summary.name
        << " material=" << summary.material
        << " draw=" << draw_kind_name(summary.drawKind)
        << " instances=" << summary.instanceCount
        << " vertices=" << summary.vertexCount
        << " channels=" << summary.channels.size()
        << " bindings=" << summary.bindings.size()
        << " bindingBytes=" << summary.totalBindingBytes;
    for (const auto& channel : summary.channels) {
        out << "\n  channel " << channel_kind_name(channel.kind)
            << " binding=" << channel.binding
            << " offset=" << channel.offset
            << " stride=" << channel.stride;
    }
    return out.str();
}

std::string format_frame_graph_summary(const FrameGraphSummary& summary) {
    std::ostringstream out;
    out << "FrameGraph"
        << " nodes=" << summary.nodes.size()
        << " computeNodes=" << summary.computeNodeCount
        << " renderNodes=" << summary.renderNodeCount
        << " renderBatches=" << summary.renderBatchCount
        << " instances=" << summary.totalInstanceCount;
    for (const auto& node : summary.nodes) {
        out << "\n  node " << node.id
            << " name=" << node.name
            << " kind=" << node_kind_name(node.kind)
            << " batches=" << node.renderBatchCount
            << " instances=" << node.instanceCount;
    }
    for (const auto& dependency : summary.dependencies) {
        out << "\n  dependency " << dependency.before
            << "->" << dependency.after;
    }
    for (const auto& usage : summary.resourceUsages) {
        out << "\n  resource_usage node=" << usage.node
            << " resource=" << usage.resourceId
            << " name=" << usage.resourceName
            << " access=" << resource_access_name(usage.access);
    }
    for (const auto& resource : summary.resourceLabels) {
        out << "\n  resource " << resource.resourceId
            << " name=" << resource.name;
    }
    return out.str();
}

std::string format_renderer_stats_summary(
    const RendererStatsSummary& summary) {
    std::ostringstream out;
    out << "RendererStats"
        << " computeNodes=" << summary.computeNodesExecuted
        << " renderNodes=" << summary.renderNodesExecuted
        << " renderBatches=" << summary.renderBatchesExecuted
        << " presented=" << (summary.presented ? "true" : "false");
    return out.str();
}

} // namespace truffle::diagnostics

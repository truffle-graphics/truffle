#include "truffle/diagnostics/diagnostics.hpp"

#include <algorithm>
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

const char* severity_name(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "Info";
    case DiagnosticSeverity::Warning:
        return "Warning";
    case DiagnosticSeverity::Error:
        return "Error";
    }
    return "Unknown";
}

const char* asset_validation_issue_kind_name(
    assets::AssetValidationIssueKind kind) noexcept {
    switch (kind) {
    case assets::AssetValidationIssueKind::MissingGroup:
        return "MissingGroup";
    case assets::AssetValidationIssueKind::MissingGeometryStream:
        return "MissingGeometryStream";
    case assets::AssetValidationIssueKind::MissingTexture:
        return "MissingTexture";
    case assets::AssetValidationIssueKind::MissingMesh:
        return "MissingMesh";
    case assets::AssetValidationIssueKind::InvalidMaterialReference:
        return "InvalidMaterialReference";
    case assets::AssetValidationIssueKind::MissingMaterial:
        return "MissingMaterial";
    case assets::AssetValidationIssueKind::MissingAttribute:
        return "MissingAttribute";
    }
    return "Unknown";
}

const char* asset_attribute_name(assets::AttributeSemantic semantic) noexcept {
    switch (semantic) {
    case assets::AttributeSemantic::Position:
        return "Position";
    case assets::AttributeSemantic::Transform:
        return "Transform";
    case assets::AttributeSemantic::LocalTransform:
        return "LocalTransform";
    case assets::AttributeSemantic::Color:
        return "Color";
    case assets::AttributeSemantic::Normal:
        return "Normal";
    case assets::AttributeSemantic::TexCoord:
        return "TexCoord";
    case assets::AttributeSemantic::ParentIndex:
        return "ParentIndex";
    case assets::AttributeSemantic::Scale:
        return "Scale";
    case assets::AttributeSemantic::Radius:
        return "Radius";
    case assets::AttributeSemantic::Velocity:
        return "Velocity";
    case assets::AttributeSemantic::Intensity:
        return "Intensity";
    case assets::AttributeSemantic::Confidence:
        return "Confidence";
    case assets::AttributeSemantic::Classification:
        return "Classification";
    case assets::AttributeSemantic::Custom:
        return "Custom";
    }
    return "Unknown";
}

bool contains_asset_id(const std::vector<assets::AssetId>& ids,
                       assets::AssetId id) noexcept {
    return std::any_of(
        ids.begin(), ids.end(),
        [id](assets::AssetId candidate) {
            return candidate == id;
        });
}

void append_unique_asset_id(std::vector<assets::AssetId>& ids,
                            assets::AssetId id) {
    if (!contains_asset_id(ids, id)) {
        ids.push_back(id);
    }
}

bool contains_string(const std::vector<std::string>& values,
                     const std::string& value) noexcept {
    return std::any_of(
        values.begin(), values.end(),
        [&value](const std::string& candidate) {
            return candidate == value;
        });
}

void append_unique_string(std::vector<std::string>& values,
                          const std::string& value) {
    if (!contains_string(values, value)) {
        values.push_back(value);
    }
}

std::vector<assets::AssetId> resolve_asset_group_ids(
    const assets::AssetCatalog& catalog,
    const AssetCatalogInspectionOptions& options) {
    std::vector<assets::AssetId> ids;
    ids.reserve(options.groupIds.size());
    for (const auto groupId : options.groupIds) {
        append_unique_asset_id(ids, groupId);
    }
    for (const auto& tag : options.groupTags) {
        for (const auto groupId : catalog.group_ids_with_tag(tag)) {
            append_unique_asset_id(ids, groupId);
        }
    }
    if (options.includeGroups && ids.empty() && options.groupTags.empty()) {
        ids = catalog.group_ids();
    }
    return ids;
}

bool matches_debug_overlay_filter(
    const DebugOverlayMetadata& metadata,
    const DebugOverlayInspectionOptions& options) {
    const auto hasGroupFilter = !options.groupIds.empty();
    const auto hasTagFilter = !options.tags.empty();
    if (!hasGroupFilter && !hasTagFilter) {
        return true;
    }
    if (hasGroupFilter && contains_asset_id(options.groupIds, metadata.group)) {
        return true;
    }
    if (hasTagFilter) {
        for (const auto& tag : metadata.tags) {
            if (contains_string(options.tags, tag)) {
                return true;
            }
        }
    }
    return false;
}

void expand_bounds(DebugOverlayBounds& bounds, DebugVec3 point) noexcept {
    if (!bounds.valid) {
        bounds.valid = true;
        bounds.min = point;
        bounds.max = point;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

void record_debug_overlay_metadata(DebugOverlaySummary& summary,
                                   const DebugOverlayMetadata& metadata) {
    if (metadata.group.valid()) {
        append_unique_asset_id(summary.groups, metadata.group);
    }
    for (const auto& tag : metadata.tags) {
        append_unique_string(summary.tags, tag);
    }
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

void append_budget_finding(std::vector<DiagnosticFinding>& findings,
                           std::string code,
                           std::string message,
                           std::uint64_t observed,
                           std::uint64_t limit) {
    if (limit == 0 || observed <= limit) {
        return;
    }
    findings.push_back({
        .severity = DiagnosticSeverity::Warning,
        .code = std::move(code),
        .message = std::move(message),
        .observed = observed,
        .limit = limit,
    });
}

} // namespace

AssetCatalogSummary summarize_asset_catalog(
    const assets::AssetCatalog& catalog) {
    return summarize_asset_catalog(catalog, {});
}

AssetCatalogSummary summarize_asset_catalog(
    const assets::AssetCatalog& catalog,
    const AssetCatalogInspectionOptions& options) {
    AssetCatalogSummary summary;
    summary.name = options.name;
    summary.stats = catalog.stats();
    if (options.validateMeshMaterials) {
        summary.validation = catalog.validate_all_mesh_materials();
    }
    const auto groupIds = resolve_asset_group_ids(catalog, options);
    summary.groups.reserve(groupIds.size());
    for (const auto groupId : groupIds) {
        AssetGroupSummary groupSummary;
        groupSummary.group = groupId;
        if (auto stats = catalog.group_stats(groupId); stats.ok()) {
            groupSummary.name = stats.value().name;
            groupSummary.tags = stats.value().tags;
            groupSummary.stats = stats.value().stats;
        }
        if (options.validateMeshMaterials) {
            groupSummary.validation = catalog.validate_group(groupId);
        }
        summary.groups.push_back(std::move(groupSummary));
    }
    return summary;
}

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

DebugOverlaySummary summarize_debug_overlay(const DebugOverlayLayer& layer) {
    return summarize_debug_overlay(layer, {});
}

DebugOverlaySummary summarize_debug_overlay(
    const DebugOverlayLayer& layer,
    const DebugOverlayInspectionOptions& options) {
    DebugOverlaySummary summary;
    summary.name = layer.name;

    for (const auto& line : layer.lines) {
        if (!matches_debug_overlay_filter(line.metadata, options)) {
            continue;
        }
        ++summary.lineCount;
        record_debug_overlay_metadata(summary, line.metadata);
        expand_bounds(summary.bounds, line.begin);
        expand_bounds(summary.bounds, line.end);
    }
    for (const auto& box : layer.boxes) {
        if (!matches_debug_overlay_filter(box.metadata, options)) {
            continue;
        }
        ++summary.boxCount;
        record_debug_overlay_metadata(summary, box.metadata);
        expand_bounds(summary.bounds, box.min);
        expand_bounds(summary.bounds, box.max);
    }
    for (const auto& point : layer.points) {
        if (!matches_debug_overlay_filter(point.metadata, options)) {
            continue;
        }
        ++summary.pointCount;
        record_debug_overlay_metadata(summary, point.metadata);
        expand_bounds(summary.bounds, point.position);
    }
    for (const auto& label : layer.labels) {
        if (!matches_debug_overlay_filter(label.metadata, options)) {
            continue;
        }
        ++summary.labelCount;
        record_debug_overlay_metadata(summary, label.metadata);
        expand_bounds(summary.bounds, label.position);
    }
    for (const auto& target : layer.pickTargets) {
        if (!matches_debug_overlay_filter(target.metadata, options)) {
            continue;
        }
        ++summary.pickTargetCount;
        record_debug_overlay_metadata(summary, target.metadata);
        expand_bounds(summary.bounds, target.min);
        expand_bounds(summary.bounds, target.max);
    }

    summary.primitiveCount =
        summary.lineCount + summary.boxCount + summary.pointCount +
        summary.labelCount + summary.pickTargetCount;
    return summary;
}

std::vector<DiagnosticFinding> evaluate_render_batch_budget(
    const RenderBatchSummary& summary,
    const RenderBatchBudget& budget) {
    std::vector<DiagnosticFinding> findings;
    append_budget_finding(
        findings, "render_batch.instances",
        "Render batch instance count exceeds budget", summary.instanceCount,
        budget.maxInstances);
    append_budget_finding(
        findings, "render_batch.vertices",
        "Render batch vertex count exceeds budget", summary.vertexCount,
        budget.maxVertexCount);
    append_budget_finding(
        findings, "render_batch.channels",
        "Render batch channel count exceeds budget",
        static_cast<std::uint64_t>(summary.channels.size()),
        budget.maxChannels);
    append_budget_finding(
        findings, "render_batch.bindings",
        "Render batch binding count exceeds budget",
        static_cast<std::uint64_t>(summary.bindings.size()),
        budget.maxBindings);
    append_budget_finding(
        findings, "render_batch.binding_bytes",
        "Render batch binding byte range exceeds budget",
        static_cast<std::uint64_t>(summary.totalBindingBytes),
        budget.maxBindingBytes);
    return findings;
}

std::vector<DiagnosticFinding> evaluate_frame_graph_budget(
    const FrameGraphSummary& summary,
    const FrameGraphBudget& budget) {
    std::vector<DiagnosticFinding> findings;
    append_budget_finding(
        findings, "frame_graph.compute_nodes",
        "Frame graph compute node count exceeds budget",
        summary.computeNodeCount, budget.maxComputeNodes);
    append_budget_finding(
        findings, "frame_graph.render_nodes",
        "Frame graph render node count exceeds budget",
        summary.renderNodeCount, budget.maxRenderNodes);
    append_budget_finding(
        findings, "frame_graph.render_batches",
        "Frame graph render batch count exceeds budget",
        summary.renderBatchCount, budget.maxRenderBatches);
    append_budget_finding(
        findings, "frame_graph.instances",
        "Frame graph instance count exceeds budget",
        summary.totalInstanceCount, budget.maxInstances);
    append_budget_finding(
        findings, "frame_graph.dependencies",
        "Frame graph dependency edge count exceeds budget",
        static_cast<std::uint64_t>(summary.dependencies.size()),
        budget.maxDependencies);
    append_budget_finding(
        findings, "frame_graph.resource_usages",
        "Frame graph resource usage declaration count exceeds budget",
        static_cast<std::uint64_t>(summary.resourceUsages.size()),
        budget.maxResourceUsages);
    return findings;
}

core::Result<DiagnosticsBundle> collect_diagnostics_bundle(
    const DiagnosticsBundleOptions& options) {
    DiagnosticsBundle bundle;

    if (options.assetCatalog != nullptr) {
        bundle.hasAssetCatalog = true;
        bundle.assetCatalog = summarize_asset_catalog(
            *options.assetCatalog, options.assetCatalogOptions);
    }

    bundle.renderBatches.reserve(options.renderBatches.size());
    for (const auto& target : options.renderBatches) {
        if (target.batch == nullptr) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Diagnostics: render batch inspection target is null");
        }
        auto summary = summarize_render_batch(*target.batch, target.options);
        const auto findings =
            evaluate_render_batch_budget(summary, options.renderBatchBudget);
        bundle.findings.insert(bundle.findings.end(),
                               findings.begin(),
                               findings.end());
        bundle.renderBatches.push_back(std::move(summary));
    }

    if (options.frameGraph != nullptr) {
        auto graphSummary = summarize_frame_graph(
            *options.frameGraph, options.frameGraphOptions);
        if (!graphSummary.ok()) {
            return graphSummary.status();
        }
        bundle.hasFrameGraph = true;
        bundle.frameGraph = graphSummary.value();
        const auto findings = evaluate_frame_graph_budget(
            bundle.frameGraph, options.frameGraphBudget);
        bundle.findings.insert(bundle.findings.end(),
                               findings.begin(),
                               findings.end());
    }

    if (options.rendererStats != nullptr) {
        bundle.hasRendererStats = true;
        bundle.rendererStats = summarize_renderer_stats(*options.rendererStats);
    }

    if (options.debugOverlay != nullptr) {
        bundle.hasDebugOverlay = true;
        bundle.debugOverlay = summarize_debug_overlay(
            *options.debugOverlay, options.debugOverlayOptions);
    }

    return bundle;
}

std::string format_asset_catalog_summary(const AssetCatalogSummary& summary) {
    std::ostringstream out;
    out << "AssetCatalog"
        << " name=" << summary.name
        << " geometryStreams=" << summary.stats.geometryStreamCount
        << " textures=" << summary.stats.textureCount
        << " materials=" << summary.stats.materialCount
        << " meshes=" << summary.stats.meshCount
        << " cpuStreams=" << summary.stats.cpuGeometryStreamCount
        << " gpuStreams=" << summary.stats.gpuResidentGeometryStreamCount
        << " externalStreams=" << summary.stats.externalGeometryStreamCount
        << " elements=" << summary.stats.totalGeometryElements
        << " bytes=" << summary.stats.totalGeometryBytes
        << " largestStream=" << summary.stats.largestGeometryStream.value
        << " largestStreamBytes=" << summary.stats.largestGeometryStreamBytes
        << " validationIssues=" << summary.validation.issues.size();
    for (const auto& issue : summary.validation.issues) {
        out << "\n  asset_issue kind="
            << asset_validation_issue_kind_name(issue.kind)
            << " asset=" << issue.asset.value
            << " group=" << issue.group.value
            << " mesh=" << issue.mesh.value
            << " material=" << issue.material.value
            << " attribute=" << asset_attribute_name(issue.attribute)
            << " message=" << issue.message;
    }
    for (const auto& group : summary.groups) {
        out << "\n  asset_group id=" << group.group.value
            << " name=" << group.name
            << " geometryStreams=" << group.stats.geometryStreamCount
            << " textures=" << group.stats.textureCount
            << " materials=" << group.stats.materialCount
            << " meshes=" << group.stats.meshCount
            << " elements=" << group.stats.totalGeometryElements
            << " bytes=" << group.stats.totalGeometryBytes
            << " validationIssues=" << group.validation.issues.size();
        for (const auto& tag : group.tags) {
            out << "\n    tag " << tag;
        }
        for (const auto& issue : group.validation.issues) {
            out << "\n    asset_issue kind="
                << asset_validation_issue_kind_name(issue.kind)
                << " asset=" << issue.asset.value
                << " group=" << issue.group.value
                << " mesh=" << issue.mesh.value
                << " material=" << issue.material.value
                << " attribute=" << asset_attribute_name(issue.attribute)
                << " message=" << issue.message;
        }
    }
    return out.str();
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

std::string format_debug_overlay_summary(
    const DebugOverlaySummary& summary) {
    std::ostringstream out;
    out << "DebugOverlay"
        << " name=" << summary.name
        << " primitives=" << summary.primitiveCount
        << " lines=" << summary.lineCount
        << " boxes=" << summary.boxCount
        << " points=" << summary.pointCount
        << " labels=" << summary.labelCount
        << " pickTargets=" << summary.pickTargetCount
        << " bounds=" << (summary.bounds.valid ? "true" : "false");
    if (summary.bounds.valid) {
        out << " min=(" << summary.bounds.min.x
            << "," << summary.bounds.min.y
            << "," << summary.bounds.min.z
            << ") max=(" << summary.bounds.max.x
            << "," << summary.bounds.max.y
            << "," << summary.bounds.max.z
            << ")";
    }
    for (const auto group : summary.groups) {
        out << "\n  group " << group.value;
    }
    for (const auto& tag : summary.tags) {
        out << "\n  tag " << tag;
    }
    return out.str();
}

std::string format_diagnostic_findings(
    const std::vector<DiagnosticFinding>& findings) {
    std::ostringstream out;
    out << "DiagnosticFindings count=" << findings.size();
    for (const auto& finding : findings) {
        out << "\n  finding severity=" << severity_name(finding.severity)
            << " code=" << finding.code
            << " observed=" << finding.observed
            << " limit=" << finding.limit
            << " message=" << finding.message;
    }
    return out.str();
}

std::string format_diagnostics_bundle(const DiagnosticsBundle& bundle) {
    std::ostringstream out;
    out << "DiagnosticsBundle"
        << " assetCatalog=" << (bundle.hasAssetCatalog ? "true" : "false")
        << " renderBatches=" << bundle.renderBatches.size()
        << " frameGraph=" << (bundle.hasFrameGraph ? "true" : "false")
        << " rendererStats=" << (bundle.hasRendererStats ? "true" : "false")
        << " debugOverlay=" << (bundle.hasDebugOverlay ? "true" : "false")
        << " findings=" << bundle.findings.size();

    if (bundle.hasAssetCatalog) {
        out << "\n" << format_asset_catalog_summary(bundle.assetCatalog);
    }
    for (const auto& batch : bundle.renderBatches) {
        out << "\n" << format_render_batch_summary(batch);
    }
    if (bundle.hasFrameGraph) {
        out << "\n" << format_frame_graph_summary(bundle.frameGraph);
    }
    if (bundle.hasRendererStats) {
        out << "\n" << format_renderer_stats_summary(bundle.rendererStats);
    }
    if (bundle.hasDebugOverlay) {
        out << "\n" << format_debug_overlay_summary(bundle.debugOverlay);
    }
    if (!bundle.findings.empty()) {
        out << "\n" << format_diagnostic_findings(bundle.findings);
    }

    return out.str();
}

} // namespace truffle::diagnostics

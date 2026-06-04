#pragma once

#include "truffle/core/status.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace truffle::render {

enum class FrameGraphNodeKind {
    Compute,
    Render,
};

enum class ResourceAccess {
    Read,
    Write,
};

struct NodeResourceUsage {
    std::uint64_t resourceId = 0;
    ResourceAccess access = ResourceAccess::Read;
};

class IFrameGraphNode {
public:
    virtual ~IFrameGraphNode() = default;
    [[nodiscard]] virtual FrameGraphNodeKind kind() const noexcept = 0;
};

class ComputePassNode final : public IFrameGraphNode {
public:
    explicit ComputePassNode(TransformComputePass& pass, TransformComputePassDesc desc)
        : pass_(&pass), desc_(std::move(desc)) {}

    [[nodiscard]] FrameGraphNodeKind kind() const noexcept override {
        return FrameGraphNodeKind::Compute;
    }

    TransformComputePass* pass() const noexcept { return pass_; }
    const TransformComputePassDesc& desc() const noexcept { return desc_; }

private:
    TransformComputePass* pass_ = nullptr;
    TransformComputePassDesc desc_;
};

class RenderPassNode final : public IFrameGraphNode {
public:
    // If desc is std::nullopt, it falls back to the swapchain default or minimal headless desc.
    explicit RenderPassNode(bool useSwapchain, std::vector<RenderBatch> batches)
        : useSwapchain_(useSwapchain), batches_(std::move(batches)) {}

    explicit RenderPassNode(rhi::RenderPassDesc desc, std::vector<RenderBatch> batches)
        : useSwapchain_(false), explicitDesc_(std::make_unique<rhi::RenderPassDesc>(desc)), batches_(std::move(batches)) {}

    [[nodiscard]] FrameGraphNodeKind kind() const noexcept override {
        return FrameGraphNodeKind::Render;
    }

    bool uses_swapchain() const noexcept { return useSwapchain_; }
    const rhi::RenderPassDesc* explicit_desc() const noexcept { return explicitDesc_.get(); }
    std::span<const RenderBatch> batches() const noexcept { return batches_; }

private:
    bool useSwapchain_ = false;
    std::unique_ptr<rhi::RenderPassDesc> explicitDesc_;
    std::vector<RenderBatch> batches_;
};

class FrameGraph {
public:
    using NodeId = std::uint32_t;

    NodeId add_node(std::unique_ptr<IFrameGraphNode> node) {
        nodes_.push_back(std::move(node));
        resourceUsages_.emplace_back();
        return static_cast<NodeId>(nodes_.size() - 1);
    }

    [[nodiscard]] core::Status add_dependency(NodeId before, NodeId after) {
        if (before >= nodes_.size() || after >= nodes_.size()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "FrameGraph: dependency references unknown node id");
        }
        if (before == after) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "FrameGraph: self dependency is not allowed");
        }
        dependencies_.emplace_back(before, after);
        return core::Status::success();
    }

    [[nodiscard]] core::Status add_resource_usage(NodeId nodeId,
                                                  NodeResourceUsage usage) {
        if (nodeId >= nodes_.size()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "FrameGraph: resource usage references unknown node id");
        }
        resourceUsages_[nodeId].push_back(usage);
        return core::Status::success();
    }

    [[nodiscard]] core::Result<std::vector<NodeId>> resolve_execution_order() const {
        std::vector<NodeId> order;
        order.reserve(nodes_.size());

        if (nodes_.empty()) {
            return order;
        }

        std::vector<std::pair<NodeId, NodeId>> allEdges = dependencies_;
        auto append_edge = [&](NodeId before, NodeId after) {
            allEdges.emplace_back(before, after);
        };

        std::unordered_set<std::uint64_t> explicitPairEdges;
        for (const auto& edge : dependencies_) {
            const auto lo = edge.first < edge.second ? edge.first : edge.second;
            const auto hi = edge.first < edge.second ? edge.second : edge.first;
            const std::uint64_t pairKey =
                (static_cast<std::uint64_t>(lo) << 32u) |
                static_cast<std::uint64_t>(hi);
            explicitPairEdges.insert(pairKey);
        }

        for (NodeId before = 0; before < nodes_.size(); ++before) {
            for (NodeId after = before + 1; after < nodes_.size(); ++after) {
                bool hazard = false;
                for (const auto& lhs : resourceUsages_[before]) {
                    for (const auto& rhs : resourceUsages_[after]) {
                        if (lhs.resourceId != rhs.resourceId) {
                            continue;
                        }
                        if (lhs.access == ResourceAccess::Write ||
                            rhs.access == ResourceAccess::Write) {
                            hazard = true;
                            break;
                        }
                    }
                    if (hazard) {
                        break;
                    }
                }
                if (hazard) {
                    const std::uint64_t pairKey =
                        (static_cast<std::uint64_t>(before) << 32u) |
                        static_cast<std::uint64_t>(after);
                    if (explicitPairEdges.contains(pairKey)) {
                        // Keep caller-defined ordering when a direct dependency exists.
                        continue;
                    }
                    append_edge(before, after);
                }
            }
        }

        std::vector<std::uint32_t> indegree(nodes_.size(), 0);
        std::vector<std::vector<NodeId>> outgoing(nodes_.size());
        std::unordered_set<std::uint64_t> dedupe;
        for (const auto& edge : allEdges) {
            const auto before = edge.first;
            const auto after = edge.second;
            if (before >= nodes_.size() || after >= nodes_.size()) {
                return core::Status::failure(
                    core::StatusCode::invalid_argument,
                    "FrameGraph: dependency graph contains invalid node id");
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(before) << 32u) |
                static_cast<std::uint64_t>(after);
            if (!dedupe.insert(key).second) {
                continue;
            }
            outgoing[before].push_back(after);
            ++indegree[after];
        }

        std::deque<NodeId> ready;
        for (NodeId nodeId = 0; nodeId < nodes_.size(); ++nodeId) {
            if (indegree[nodeId] == 0) {
                ready.push_back(nodeId);
            }
        }

        while (!ready.empty()) {
            const NodeId nodeId = ready.front();
            ready.pop_front();
            order.push_back(nodeId);

            for (const NodeId next : outgoing[nodeId]) {
                --indegree[next];
                if (indegree[next] == 0) {
                    ready.push_back(next);
                }
            }
        }

        if (order.size() != nodes_.size()) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "FrameGraph: dependency cycle detected");
        }

        return order;
    }

    std::span<const std::unique_ptr<IFrameGraphNode>> nodes() const noexcept {
        return nodes_;
    }

    [[nodiscard]] const IFrameGraphNode* node(NodeId id) const noexcept {
        if (id >= nodes_.size()) {
            return nullptr;
        }
        return nodes_[id].get();
    }

private:
    std::vector<std::vector<NodeResourceUsage>> resourceUsages_;
    std::vector<std::pair<NodeId, NodeId>> dependencies_;
    std::vector<std::unique_ptr<IFrameGraphNode>> nodes_;
};

} // namespace truffle::render

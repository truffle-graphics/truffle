#pragma once

#include "truffle/render/render_batch.hpp"
#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <span>
#include <vector>

namespace truffle::render {

enum class FrameGraphNodeKind {
    Compute,
    Render,
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
    void add_node(std::unique_ptr<IFrameGraphNode> node) {
        nodes_.push_back(std::move(node));
    }

    std::span<const std::unique_ptr<IFrameGraphNode>> nodes() const noexcept {
        return nodes_;
    }

private:
    std::vector<std::unique_ptr<IFrameGraphNode>> nodes_;
};

} // namespace truffle::render

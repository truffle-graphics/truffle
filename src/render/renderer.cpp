#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/rhi/shader_reflection.hpp"

#include <array>
#include <memory>
#include <string>

namespace truffle::render {

namespace {

core::Status validate_render_batch_bindings(const RenderBatch& batch,
                                            const rhi::IPipeline& pipeline) {
    const auto* reflection = pipeline.reflection();
    if (!reflection) {
        return core::Status::success();
    }

    constexpr std::size_t kBindingSlots = RenderBatch::kMaxBindings + 1;
    std::array<bool, kBindingSlots> expectedBufferBindings{};
    for (std::size_t i = 0; i < reflection->get_binding_count(); ++i) {
        const auto& binding = reflection->get_binding_info(i);
        if (binding.type == rhi::ResourceBindingType::Buffer &&
            (binding.stage == rhi::ShaderStage::vertex ||
             binding.stage == rhi::ShaderStage::fragment)) {
            if (binding.bindingIndex >= kBindingSlots) {
                return core::Status::failure(
                    core::StatusCode::invalid_argument,
                    "Renderer: pipeline reflection binding index exceeds supported range");
            }
            expectedBufferBindings[binding.bindingIndex] = true;
        }
    }

    std::array<bool, kBindingSlots> providedBufferBindings{};
    for (std::uint32_t i = 0; i < RenderBatch::kMaxBindings; ++i) {
        if (batch.bindings[i].buffer) {
            providedBufferBindings[i] = true;
        }
    }

    constexpr std::uint32_t kUniformBinding = RenderBatch::kMaxBindings;
    if (batch.uniformBuffer.buffer) {
        providedBufferBindings[kUniformBinding] = true;
    }

    for (std::size_t i = 0; i < kBindingSlots; ++i) {
        if (expectedBufferBindings[i] && !providedBufferBindings[i]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: missing buffer binding required by pipeline reflection");
        }
    }

    for (std::size_t i = 0; i < kBindingSlots; ++i) {
        if (providedBufferBindings[i] && !expectedBufferBindings[i]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: unexpected buffer binding not declared by pipeline reflection");
        }
    }

    return core::Status::success();
}

} // namespace

// ---------------------------------------------------------------------------
// NullPipelineCache
// ---------------------------------------------------------------------------

NullPipelineCache::NullPipelineCache(rhi::IDevice& device) : device_(&device) {}

rhi::IPipeline* NullPipelineCache::get_or_create(const InstanceLayout& /*layout*/,
                                              MaterialId /*material*/,
                                              std::size_t /*variantHash*/) {
    if (!pipeline_) {
        auto result = device_->create_pipeline({.debugName = "null_pipeline"});
        if (result.ok()) {
            pipeline_ = std::move(result).value();
        }
    }
    return pipeline_.get();
}

// ---------------------------------------------------------------------------
// PipelineCache
// ---------------------------------------------------------------------------

PipelineCache::PipelineCache(rhi::IDevice& device, rhi::TextureFormat colorFormat)
    : device_(&device), colorFormat_(colorFormat) {}

void PipelineCache::register_shaders(MaterialId material, const ShaderBinding& shaders) {
    shaders_[material] = shaders;
}

rhi::IPipeline* PipelineCache::get_or_create(const InstanceLayout& layout,
                                              MaterialId material,
                                              std::size_t variantHash) {
    const CacheKey key{layout.hash(), material, variantHash};
    if (auto it = pipelines_.find(key); it != pipelines_.end()) {
        return it->second.get();
    }

    auto sit = shaders_.find(material);
    if (sit == shaders_.end()) {
        return nullptr; // no shaders registered for this material
    }
    const auto& sb = sit->second;

    const std::string debugName =
        "pipeline_mat" + std::to_string(static_cast<std::uint64_t>(material));
    auto result = device_->create_pipeline({
        .debugName      = debugName,
        .vertexShader   = sb.vertexShader,
        .fragmentShader = sb.fragmentShader,
        .colorFormat    = colorFormat_,
    });
    if (!result.ok()) {
        return nullptr;
    }
    auto& ptr = pipelines_[key];
    ptr = std::move(result).value();
    return ptr.get();
}

void PipelineCache::invalidate(MaterialId material) {
    for (auto it = pipelines_.begin(); it != pipelines_.end(); ) {
        if (it->first.material == material) {
            it = pipelines_.erase(it);
        } else {
            ++it;
        }
    }
}

void PipelineCache::invalidate_all() {
    pipelines_.clear();
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

Renderer::Renderer(rhi::IDevice& device, IPipelineCache* cache)
    : device_(&device), cache_(cache) {
    if (!cache_) {
        defaultCache_ = std::make_unique<NullPipelineCache>(device);
        cache_ = defaultCache_.get();
    }
}

core::Status Renderer::render(const FrameGraph& graph, rhi::ISwapchain* swapchain) {
    lastFrameStats_ = {};

    const auto executionOrder = graph.resolve_execution_order();
    if (!executionOrder.ok()) {
        return executionOrder.status();
    }

    auto cmd = device_->create_command_buffer();
    if (const auto s = cmd->begin(); !s.ok()) {
        return s;
    }

    rhi::ITexture* swapchainDrawable = nullptr;
    bool swapchainAcquired = false;

    for (const auto nodeId : executionOrder.value()) {
        const auto* nodePtr = graph.node(nodeId);
        if (!nodePtr) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: frame graph referenced an unknown node id");
        }

        if (nodePtr->kind() == FrameGraphNodeKind::Compute) {
            auto* node = static_cast<const ComputePassNode*>(nodePtr);
            if (const auto s = node->pass()->dispatch(*cmd, node->desc()); !s.ok()) {
                return s;
            }
            ++lastFrameStats_.computeNodesExecuted;
        } else if (nodePtr->kind() == FrameGraphNodeKind::Render) {
            auto* node = static_cast<const RenderPassNode*>(nodePtr);
            ++lastFrameStats_.renderNodesExecuted;
            lastFrameStats_.renderBatchesExecuted +=
                static_cast<std::uint32_t>(node->batches().size());
            
            rhi::RenderPassDesc passDesc;
            if (node->uses_swapchain()) {
                if (!swapchainAcquired && swapchain) {
                    swapchainDrawable = swapchain->acquire_next_texture();
                    swapchainAcquired = true;
                }
                if (swapchain) {
                    passDesc.extent = swapchain->desc().extent;
                    passDesc.colorAttachment.texture = swapchainDrawable;
                    passDesc.colorAttachment.loadOp = rhi::LoadOp::clear;
                    passDesc.colorAttachment.storeOp = rhi::StoreOp::store;
                } else {
                    passDesc.extent = {1, 1}; // headless / null-backend
                }
            } else if (node->explicit_desc()) {
                passDesc = *node->explicit_desc();
            }

            if (const auto s = cmd->begin_render_pass(passDesc); !s.ok()) {
                return s;
            }

            for (const auto& batch : node->batches()) {
                auto* pipeline = cache_->get_or_create(
                    batch.layout, batch.material, batch.variantHash);
                if (!pipeline) {
                    return core::Status::failure(
                        core::StatusCode::invalid_state,
                        "Renderer: no graphics pipeline available for render batch");
                }
                if (const auto s = cmd->bind_pipeline(*pipeline); !s.ok()) {
                    return s;
                }
                if (const auto s = validate_render_batch_bindings(batch, *pipeline);
                    !s.ok()) {
                    return s;
                }

                for (std::uint32_t i = 0; i < RenderBatch::kMaxBindings; ++i) {
                    if (batch.bindings[i].buffer) {
                        (void)cmd->bind_vertex_buffer(
                            i, *batch.bindings[i].buffer, batch.bindings[i].offset);
                    }
                }

                if (batch.uniformBuffer.buffer) {
                    constexpr std::uint32_t kUniformBinding = RenderBatch::kMaxBindings;
                    (void)cmd->bind_uniform_buffer(
                        kUniformBinding, *batch.uniformBuffer.buffer,
                        batch.uniformBuffer.offset);
                }

                if (batch.kind == DrawKind::Indirect && batch.indirectBuffer.buffer) {
                    if (const auto s = cmd->draw_indirect(*batch.indirectBuffer.buffer, batch.indirectBuffer.offset); !s.ok()) return s;
                } else if (batch.kind == DrawKind::IndirectIndexed && batch.indirectBuffer.buffer && batch.indexBuffer.buffer) {
                    (void)cmd->bind_index_buffer(*batch.indexBuffer.buffer, batch.indexBuffer.offset, batch.indexFormat);
                    if (const auto s = cmd->draw_indexed_indirect(*batch.indirectBuffer.buffer, batch.indirectBuffer.offset); !s.ok()) return s;
                } else if (batch.kind == DrawKind::Indexed && batch.indexBuffer.buffer) {
                    (void)cmd->bind_index_buffer(*batch.indexBuffer.buffer, batch.indexBuffer.offset, batch.indexFormat);
                    if (const auto s = cmd->draw_indexed_instanced(batch.vertexCount, batch.instanceCount); !s.ok()) return s;
                } else {
                    if (const auto s = cmd->draw_instanced(batch.vertexCount, batch.instanceCount); !s.ok()) return s;
                }
            }

            if (const auto s = cmd->end_render_pass(); !s.ok()) {
                return s;
            }
        }
    }

    if (swapchainAcquired && swapchain) {
        if (const auto s = swapchain->schedule_present(*cmd); !s.ok()) {
            return s;
        }
        lastFrameStats_.presented = true;
    }
    if (const auto s = cmd->end(); !s.ok()) {
        return s;
    }
    return device_->queue(rhi::QueueKind::graphics).submit(*cmd);
}

} // namespace truffle::render

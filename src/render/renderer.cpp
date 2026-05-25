#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/renderer.hpp"

#include <string>

namespace truffle::render {

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
    : device_(&device), cache_(cache) {}

core::Status Renderer::render(std::span<const RenderBatch> batches,
                               rhi::ISwapchain* swapchain,
                               TransformComputePass* computePass,
                               const TransformComputePassDesc* computeDesc) {
    auto cmd = device_->create_command_buffer();
    if (const auto s = cmd->begin(); !s.ok()) {
        return s;
    }

    if (computePass && computeDesc) {
        if (const auto s = computePass->dispatch(*cmd, *computeDesc); !s.ok()) {
            return s;
        }
    }

    // Build render pass from swapchain or use a minimal headless descriptor
    rhi::RenderPassDesc passDesc;
    if (swapchain) {
        passDesc.extent                     = swapchain->desc().extent;
        passDesc.colorAttachment.texture    = swapchain->acquire_next_texture();
        passDesc.colorAttachment.loadOp     = rhi::LoadOp::clear;
        passDesc.colorAttachment.storeOp    = rhi::StoreOp::store;
    } else {
        passDesc.extent = {1, 1}; // headless / null-backend
    }
    if (const auto s = cmd->begin_render_pass(passDesc); !s.ok()) {
        return s;
    }

    for (const auto& batch : batches) {
        if (cache_) {
            if (auto* pipeline = cache_->get_or_create(batch.layout, batch.material, batch.variantHash)) {
                (void)cmd->bind_pipeline(*pipeline);
            }
        }

        for (std::uint32_t i = 0; i < RenderBatch::kMaxBindings; ++i) {
            if (batch.bindings[i].buffer) {
                (void)cmd->bind_vertex_buffer(
                    i, *batch.bindings[i].buffer, batch.bindings[i].offset);
            }
        }

        if (batch.uniformBuffer.buffer) {
            // Uniform buffers go immediately after the vertex binding slots.
            constexpr std::uint32_t kUniformBinding = RenderBatch::kMaxBindings;
            (void)cmd->bind_uniform_buffer(
                kUniformBinding, *batch.uniformBuffer.buffer,
                batch.uniformBuffer.offset);
        }

        if (batch.kind == DrawKind::Indirect && batch.indirectBuffer.buffer) {
            if (const auto s =
                    cmd->draw_indirect(*batch.indirectBuffer.buffer,
                                       batch.indirectBuffer.offset);
                !s.ok()) {
                return s;
            }
        } else if (batch.kind == DrawKind::IndirectIndexed && batch.indirectBuffer.buffer && batch.indexBuffer.buffer) {
            (void)cmd->bind_index_buffer(*batch.indexBuffer.buffer,
                                          batch.indexBuffer.offset,
                                          batch.indexFormat);
            if (const auto s =
                    cmd->draw_indexed_indirect(*batch.indirectBuffer.buffer,
                                               batch.indirectBuffer.offset);
                !s.ok()) {
                return s;
            }
        } else if (batch.kind == DrawKind::Indexed && batch.indexBuffer.buffer) {
            (void)cmd->bind_index_buffer(*batch.indexBuffer.buffer,
                                          batch.indexBuffer.offset,
                                          batch.indexFormat);
            if (const auto s =
                    cmd->draw_indexed_instanced(batch.vertexCount,
                                                batch.instanceCount);
                !s.ok()) {
                return s;
            }
        } else {
            if (const auto s =
                    cmd->draw_instanced(batch.vertexCount, batch.instanceCount);
                !s.ok()) {
                return s;
            }
        }
    }

    if (const auto s = cmd->end_render_pass(); !s.ok()) {
        return s;
    }
    if (swapchain) {
        if (const auto s = swapchain->schedule_present(*cmd); !s.ok()) {
            return s;
        }
    }
    if (const auto s = cmd->end(); !s.ok()) {
        return s;
    }
    return device_->queue(rhi::QueueKind::graphics).submit(*cmd);
}

} // namespace truffle::render


#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/renderer.hpp"

#include <array>
#include <string>

namespace truffle::render {
namespace {

core::Status validate_render_batch_bindings(const RenderBatch& batch,
                                            const rhi::Pipeline& pipeline) {
    if (pipeline.reflection().bindings().empty()) {
        return core::Status::success();
    }
    constexpr std::size_t kBindingSlots = RenderBatch::kMaxBindings + 1;
    std::array<bool, kBindingSlots> expected{};
    for (const auto& binding : pipeline.reflection().bindings()) {
        if (binding.type != rhi::ResourceBindingType::buffer ||
            (binding.stage != rhi::ShaderStage::vertex &&
             binding.stage != rhi::ShaderStage::fragment)) {
            continue;
        }
        if (binding.group != 0 || binding.binding >= kBindingSlots) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: reflected buffer binding exceeds the direct binding lane");
        }
        expected[binding.binding] = true;
    }

    std::array<bool, kBindingSlots> provided{};
    for (std::uint32_t index = 0; index < RenderBatch::kMaxBindings; ++index) {
        provided[index] = batch.bindings[index].buffer != nullptr;
    }
    provided[RenderBatch::kMaxBindings] = batch.uniformBuffer.buffer != nullptr;

    for (std::size_t index = 0; index < kBindingSlots; ++index) {
        if (expected[index] != provided[index]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                expected[index]
                    ? "Renderer: missing reflected buffer binding"
                    : "Renderer: unexpected buffer binding");
        }
    }
    return core::Status::success();
}

} // namespace

NullPipelineCache::NullPipelineCache(rhi::Device& device) : device_(&device) {}

core::Status NullPipelineCache::prepare_render_pass(
    const rhi::RenderPassDesc& pass) {
    std::vector<rhi::TextureFormat> colorFormats;
    colorFormats.reserve(pass.colorAttachments.size());
    std::uint32_t sampleCount = 1;
    bool sampleCountSet = false;
    for (const auto& attachment : pass.colorAttachments) {
        if (attachment.texture == nullptr) {
            return core::Status::failure(core::StatusCode::invalid_argument,
                                         "Renderer: null color attachment");
        }
        const auto desc = attachment.texture->desc();
        if (sampleCountSet && sampleCount != desc.sampleCount) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: attachment sample counts do not match");
        }
        colorFormats.push_back(desc.format);
        sampleCount = desc.sampleCount;
        sampleCountSet = true;
    }
    auto depthFormat = rhi::TextureFormat::unknown;
    if (pass.depthStencilAttachment.texture != nullptr) {
        const auto desc = pass.depthStencilAttachment.texture->desc();
        depthFormat = desc.format;
        if (sampleCountSet && sampleCount != desc.sampleCount) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: attachment sample counts do not match");
        }
        sampleCount = desc.sampleCount;
    }
    if (colorFormats != colorFormats_ || depthFormat != depthStencilFormat_ ||
        sampleCount != sampleCount_) {
        colorFormats_ = std::move(colorFormats);
        depthStencilFormat_ = depthFormat;
        sampleCount_ = sampleCount;
        pipeline_ = {};
    }
    return core::Status::success();
}

rhi::Pipeline* NullPipelineCache::get_or_create(const InstanceLayout&,
                                                MaterialId,
                                                std::size_t) {
    if (!pipeline_.valid()) {
        rhi::PipelineDesc desc;
        for (const auto format : colorFormats_) {
            rhi::ColorTargetState target;
            target.format = format;
            desc.colorTargets.push_back(target);
        }
        desc.depthStencil.format = depthStencilFormat_;
        desc.multisample.sampleCount = sampleCount_;
        desc.debugName = "null pipeline";
        auto result = device_->create_pipeline(desc);
        if (!result.ok()) {
            return nullptr;
        }
        pipeline_ = std::move(result).value();
    }
    return &pipeline_;
}

PipelineCache::PipelineCache(rhi::Device& device, rhi::TextureFormat colorFormat)
    : device_(&device), colorFormat_(colorFormat) {}

void PipelineCache::register_shaders(MaterialId material,
                                     const ShaderBinding& shaders) {
    shaders_[material] = shaders;
}

core::Status PipelineCache::prepare_render_pass(
    const rhi::RenderPassDesc& pass) {
    if (pass.colorAttachments.size() != 1 ||
        pass.colorAttachments.front().texture == nullptr ||
        pass.colorAttachments.front().texture->desc().format != colorFormat_ ||
        pass.colorAttachments.front().texture->desc().sampleCount != 1 ||
        pass.depthStencilAttachment.texture != nullptr) {
        return core::Status::failure(
            core::StatusCode::invalid_argument,
            "Renderer: pipeline cache does not match render-pass attachments");
    }
    return core::Status::success();
}

rhi::Pipeline* PipelineCache::get_or_create(const InstanceLayout& layout,
                                            MaterialId material,
                                            std::size_t variantHash) {
    const CacheKey key{layout.hash(), material, variantHash};
    if (const auto found = pipelines_.find(key); found != pipelines_.end()) {
        return &found->second;
    }
    const auto shaders = shaders_.find(material);
    if (shaders == shaders_.end()) {
        return nullptr;
    }
    rhi::PipelineDesc desc;
    desc.vertexShader = shaders->second.vertexShader;
    desc.fragmentShader = shaders->second.fragmentShader;
    rhi::ColorTargetState target;
    target.format = colorFormat_;
    desc.colorTargets.push_back(target);
    desc.debugName = "pipeline material " + std::to_string(material);
    auto result = device_->create_pipeline(desc);
    if (!result.ok()) {
        return nullptr;
    }
    const auto [inserted, added] =
        pipelines_.emplace(key, std::move(result).value());
    (void)added;
    return &inserted->second;
}

void PipelineCache::invalidate(MaterialId material) {
    for (auto current = pipelines_.begin(); current != pipelines_.end();) {
        if (current->first.material == material) {
            current = pipelines_.erase(current);
        } else {
            ++current;
        }
    }
}

void PipelineCache::invalidate_all() { pipelines_.clear(); }

Renderer::Renderer(rhi::Device& device, IPipelineCache* cache)
    : device_(&device), cache_(cache) {
    if (cache_ == nullptr) {
        defaultCache_ = std::make_unique<NullPipelineCache>(device);
        cache_ = defaultCache_.get();
    }
}

core::Status Renderer::render(const FrameGraph& graph, rhi::Swapchain* swapchain) {
    lastFrameStats_ = {};
    const auto order = graph.resolve_execution_order();
    if (!order.ok()) {
        return order.status();
    }

    auto poolResult = device_->create_command_pool(rhi::QueueKind::graphics);
    if (!poolResult.ok()) {
        return poolResult.status();
    }
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    if (!listResult.ok()) {
        return listResult.status();
    }
    auto list = std::move(listResult).value();
    if (auto status = list.begin(); !status.ok()) {
        return status;
    }

    rhi::AcquireResult acquired;
    bool hasAcquiredImage = false;

    for (const auto nodeId : order.value()) {
        const auto* node = graph.node(nodeId);
        if (node == nullptr) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "Renderer: execution order referenced an unknown node");
        }
        if (node->kind() == FrameGraphNodeKind::Compute) {
            const auto* compute = static_cast<const ComputePassNode*>(node);
            if (auto status = compute->pass()->dispatch(list, compute->desc());
                !status.ok()) {
                return status;
            }
            ++lastFrameStats_.computeNodesExecuted;
            continue;
        }

        const auto* render = static_cast<const RenderPassNode*>(node);
        rhi::RenderPassDesc pass;
        if (render->uses_swapchain()) {
            if (swapchain != nullptr && !hasAcquiredImage) {
                acquired = swapchain->acquire_next_image();
                if (!acquired.ok()) {
                    return acquired.status;
                }
                hasAcquiredImage = true;
            }
            if (swapchain != nullptr) {
                pass.extent = swapchain->desc().extent;
                rhi::ColorAttachmentDesc attachment;
                attachment.texture = acquired.image;
                pass.colorAttachments.push_back(attachment);
            }
        } else if (render->explicit_desc() != nullptr) {
            pass = *render->explicit_desc();
        }

        if (auto status = cache_->prepare_render_pass(pass); !status.ok()) {
            return status;
        }

        auto encoderResult = list.begin_rendering(pass);
        if (!encoderResult.ok()) {
            return encoderResult.status();
        }
        auto encoder = std::move(encoderResult).value();
        ++lastFrameStats_.renderNodesExecuted;

        for (const auto& batch : render->batches()) {
            auto* pipeline = cache_->get_or_create(
                batch.layout, batch.material, batch.variantHash);
            if (pipeline == nullptr) {
                return core::Status::failure(
                    core::StatusCode::invalid_state,
                    "Renderer: no graphics pipeline is available");
            }
            if (auto status = encoder.bind_pipeline(*pipeline); !status.ok()) {
                return status;
            }
            if (auto status = validate_render_batch_bindings(batch, *pipeline);
                !status.ok()) {
                return status;
            }
            for (std::uint32_t index = 0; index < RenderBatch::kMaxBindings;
                 ++index) {
                if (batch.bindings[index].buffer != nullptr) {
                    if (auto status = encoder.bind_vertex_buffer(
                            index, *batch.bindings[index].buffer,
                            batch.bindings[index].offset);
                        !status.ok()) {
                        return status;
                    }
                }
            }
            if (batch.uniformBuffer.buffer != nullptr) {
                if (auto status = encoder.bind_uniform_buffer(
                        RenderBatch::kMaxBindings, *batch.uniformBuffer.buffer,
                        batch.uniformBuffer.offset);
                    !status.ok()) {
                    return status;
                }
            }

            core::Status status;
            if ((batch.kind == DrawKind::Indirect ||
                 batch.kind == DrawKind::IndirectIndexed) &&
                batch.indirectBuffer.buffer != nullptr) {
                status = encoder.draw_indirect(
                    *batch.indirectBuffer.buffer, batch.indirectBuffer.offset,
                    batch.kind == DrawKind::IndirectIndexed);
            } else if (batch.kind == DrawKind::Indexed &&
                       batch.indexBuffer.buffer != nullptr) {
                status = encoder.bind_index_buffer(
                    *batch.indexBuffer.buffer, batch.indexBuffer.offset,
                    batch.indexFormat);
                if (status.ok()) {
                    status = encoder.draw_indexed(batch.vertexCount,
                                                  batch.instanceCount);
                }
            } else {
                status = encoder.draw(batch.vertexCount, batch.instanceCount);
            }
            if (!status.ok()) {
                return status;
            }
            ++lastFrameStats_.renderBatchesExecuted;
        }
        if (auto status = encoder.end(); !status.ok()) {
            return status;
        }
    }

    if (auto status = list.end(); !status.ok()) {
        return status;
    }
    auto queueResult = device_->queue(rhi::QueueKind::graphics);
    if (!queueResult.ok()) {
        return queueResult.status();
    }
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    if (auto status = queue.submit(lists); !status.ok()) {
        return status;
    }
    if (hasAcquiredImage) {
        if (auto status = queue.present(*swapchain, acquired.imageIndex);
            !status.ok()) {
            return status;
        }
        lastFrameStats_.presented = true;
    }
    return core::Status::success();
}

} // namespace truffle::render

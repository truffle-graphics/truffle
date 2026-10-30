#include "truffle/renderer/pipeline_cache.hpp"
#include "truffle/renderer/renderer.hpp"

#include <array>
#include <algorithm>

namespace truffle::renderer {
namespace {

base::Status validate_render_batch_bindings(const RenderBatch& batch,
                                            const rhi::Pipeline& pipeline) {
    for (std::size_t index = 0; index < batch.bindGroups.size(); ++index) {
        const auto& group = batch.bindGroups[index];
        if (group.bindings == nullptr || !group.bindings->valid() ||
            std::any_of(batch.bindGroups.begin(),
                        batch.bindGroups.begin() + static_cast<std::ptrdiff_t>(index),
                        [&](const auto& previous) { return previous.group == group.group; })) {
            return base::Status::failure(
                base::StatusCode::invalid_argument,
                "Renderer: invalid or duplicate batch descriptor group");
        }
    }
    for (const auto& binding : pipeline.reflection().bindings()) {
        if (std::none_of(batch.bindGroups.begin(), batch.bindGroups.end(),
                         [&](const auto& group) { return group.group == binding.group; })) {
            return base::Status::failure(
                base::StatusCode::invalid_argument,
                "Renderer: missing reflected descriptor group");
        }
    }
    return base::Status::success();
}

} // namespace

Renderer::Renderer(rhi::Device& device, IPipelineCache* cache)
    : device_(&device), cache_(cache) {
    if (cache_ == nullptr) {
        defaultCache_ = std::make_unique<NullPipelineCache>(device);
        cache_ = defaultCache_.get();
    }
}

base::Status Renderer::render(const FrameGraph& graph, rhi::Swapchain* swapchain) {
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
            return base::Status::failure(
                base::StatusCode::invalid_argument,
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
                rhi::BarrierBatch toRender;
                rhi::TextureBarrier barrier;
                barrier.texture = acquired.image;
                barrier.oldLayout = rhi::TextureLayout::present;
                barrier.newLayout = rhi::TextureLayout::color_attachment;
                barrier.sourceStages = rhi::PipelineStage::top;
                barrier.destinationStages =
                    rhi::PipelineStage::color_attachment_output;
                barrier.destinationAccess =
                    rhi::Access::color_attachment_write;
                toRender.textures.push_back(barrier);
                if (auto status = list.barrier(toRender); !status.ok()) {
                    return status;
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
            if (auto status = cache_->validate_batch(batch); !status.ok()) {
                return status;
            }
            auto pipelineResult = cache_->get_or_create(
                batch.layout, batch.material, batch.variantHash);
            if (!pipelineResult.ok()) {
                return pipelineResult.status();
            }
            auto* pipeline = pipelineResult.value();
            if (pipeline == nullptr || !pipeline->valid()) {
                return base::Status::failure(base::StatusCode::invalid_state,
                                             "Renderer: cache returned an invalid pipeline");
            }
            if (auto status = encoder.bind_pipeline(*pipeline); !status.ok()) {
                return status;
            }
            if (auto status = validate_render_batch_bindings(batch, *pipeline);
                !status.ok()) {
                return status;
            }
            for (const auto& group : batch.bindGroups) {
                if (auto status = encoder.bind_group(
                        group.group, *group.bindings, group.dynamicOffsets);
                    !status.ok()) {
                    return status;
                }
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

            base::Status status;
            if (batch.kind == DrawKind::IndirectIndexed) {
                status = encoder.bind_index_buffer(
                    *batch.indexBuffer.buffer, batch.indexBuffer.offset, batch.indexFormat);
                if (!status.ok()) {
                    return status;
                }
            }
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

    if (hasAcquiredImage) {
        rhi::BarrierBatch toPresent;
        rhi::TextureBarrier barrier;
        barrier.texture = acquired.image;
        barrier.oldLayout = rhi::TextureLayout::color_attachment;
        barrier.newLayout = rhi::TextureLayout::present;
        barrier.sourceStages =
            rhi::PipelineStage::color_attachment_output;
        barrier.destinationStages = rhi::PipelineStage::bottom;
        barrier.sourceAccess = rhi::Access::color_attachment_write;
        toPresent.textures.push_back(barrier);
        if (auto status = list.barrier(toPresent); !status.ok()) {
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
    std::array<rhi::SemaphoreWait, 1> acquireWaits{};
    std::array<rhi::SemaphoreSignal, 1> renderSignals{};
    rhi::Semaphore rendered;
    if (hasAcquiredImage) {
        auto renderedResult = device_->create_semaphore();
        if (!renderedResult.ok()) {
            return renderedResult.status();
        }
        rendered = std::move(renderedResult).value();
        acquireWaits[0] = {.semaphore = acquired.available,
                           .value = acquired.availableValue,
                           .stages =
                               rhi::PipelineStage::color_attachment_output};
        renderSignals[0] = {.semaphore = &rendered, .value = 1};
    }
    if (auto status = queue.submit({
            .commandLists = lists,
            .waits = hasAcquiredImage
                         ? std::span<const rhi::SemaphoreWait>{acquireWaits}
                         : std::span<const rhi::SemaphoreWait>{},
            .signals = hasAcquiredImage
                           ? std::span<const rhi::SemaphoreSignal>{renderSignals}
                           : std::span<const rhi::SemaphoreSignal>{},
        });
        !status.ok()) {
        return status;
    }
    if (hasAcquiredImage) {
        const std::array<rhi::SemaphoreWait, 1> presentWaits{{
            {.semaphore = &rendered,
             .value = 1,
             .stages = rhi::PipelineStage::bottom},
        }};
        if (auto status = queue.present({.swapchain = swapchain,
                                         .imageIndex = acquired.imageIndex,
                                         .waits = presentWaits});
            !status.ok()) {
            return status;
        }
        lastFrameStats_.presented = true;
    }
    return base::Status::success();
}

} // namespace truffle::renderer

#include "rhi_test_utils.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <thread>
#include <type_traits>
#include <vector>

int main() {
    using namespace truffle;
    static_assert(!std::is_copy_constructible_v<rhi::Buffer>);
    static_assert(std::is_nothrow_move_constructible_v<rhi::Buffer>);

    auto context = tests::make_null_context();
    assert(context.instance.backend() == rhi::BackendKind::null_validation);
    assert(context.instance.adapter_count() == 1);
    assert(context.adapter.info().validationOnly);
    assert(!context.adapter.info().native);

    auto buffer = tests::make_buffer(
        context.device, 64,
        rhi::BufferUsage::vertex | rhi::BufferUsage::copy_destination);
    const auto originalId = buffer.id();
    std::array<std::byte, 4> input{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    assert(buffer.write(8, input).ok());
    std::array<std::byte, 4> output{};
    assert(buffer.read(8, output).ok());
    assert(output == input);
    auto movedBuffer = std::move(buffer);
    assert(!buffer.valid());
    assert(movedBuffer.valid());
    assert(movedBuffer.id() == originalId);

    auto pipelineResult = context.device.create_pipeline({});
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    auto poolResult = context.device.create_command_pool(rhi::QueueKind::graphics);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.state() == rhi::CommandListState::initial);
    assert(!list.end().ok());
    assert(list.begin().ok());
    auto encoderResult = list.begin_rendering({.extent = {16, 16}});
    assert(encoderResult.ok());
    auto encoder = std::move(encoderResult).value();
    assert(!encoder.draw(3).ok());
    assert(encoder.bind_pipeline(pipeline).ok());
    assert(encoder.bind_vertex_buffer(0, movedBuffer).ok());
    assert(encoder.draw(3, 2).ok());
    assert(encoder.end().ok());

    // Recorded commands retain their payload even after the public owner dies.
    movedBuffer = {};
    assert(!movedBuffer.valid());
    auto replacementBuffer = tests::make_buffer(
        context.device, 64,
        rhi::BufferUsage::vertex | rhi::BufferUsage::copy_destination);
    assert(replacementBuffer.id() != originalId);
    assert(list.end().ok());

    auto fenceResult = context.device.create_fence();
    assert(fenceResult.ok());
    auto fence = std::move(fenceResult).value();
    auto queueResult = context.device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists, &fence, 7).ok());
    assert(list.state() == rhi::CommandListState::submitted);
    assert(fence.wait(7, std::chrono::milliseconds{1}).ok());

    auto surfaceResult = context.device.create_surface({
        .native = {},
        .initialExtent = {64, 32},
    });
    assert(surfaceResult.ok());
    auto surface = std::move(surfaceResult).value();
    auto swapchainResult = context.device.create_swapchain(
        surface, {.extent = {64, 32}, .imageCount = 3});
    assert(swapchainResult.ok());
    auto swapchain = std::move(swapchainResult).value();
    const auto acquired = swapchain.acquire_next_image();
    assert(acquired.ok());
    assert(!swapchain.acquire_next_image().ok());
    assert(swapchain.resize({96, 48}).code == rhi::StatusCode::invalid_state);
    const std::array<rhi::SemaphoreWait, 1> presentWaits{{
        {.semaphore = acquired.available,
         .value = acquired.availableValue,
         .stages = rhi::PipelineStage::bottom},
    }};
    assert(queue
               .present({.swapchain = &swapchain,
                         .imageIndex = acquired.imageIndex,
                         .waits = presentWaits})
               .ok());
    assert(swapchain.resize({128, 64}).ok());
    const auto layoutImage = swapchain.acquire_next_image();
    assert(layoutImage.ok());
    auto renderLayoutListResult = pool.allocate();
    assert(renderLayoutListResult.ok());
    auto renderLayoutList = std::move(renderLayoutListResult).value();
    assert(renderLayoutList.begin().ok());
    rhi::BarrierBatch renderLayoutBatch;
    rhi::TextureBarrier renderLayoutBarrier;
    renderLayoutBarrier.texture = layoutImage.image;
    renderLayoutBarrier.oldLayout = rhi::TextureLayout::present;
    renderLayoutBarrier.newLayout = rhi::TextureLayout::color_attachment;
    renderLayoutBarrier.destinationStages =
        rhi::PipelineStage::color_attachment_output;
    renderLayoutBarrier.destinationAccess =
        rhi::Access::color_attachment_write;
    renderLayoutBatch.textures.push_back(renderLayoutBarrier);
    assert(renderLayoutList.barrier(renderLayoutBatch).ok());
    assert(renderLayoutList.end().ok());
    std::array<rhi::CommandList*, 1> renderLayoutLists{&renderLayoutList};
    assert(queue.submit(renderLayoutLists).ok());
    assert(queue.present(swapchain, layoutImage.imageIndex).code ==
           rhi::StatusCode::invalid_state);
    auto presentLayoutListResult = pool.allocate();
    assert(presentLayoutListResult.ok());
    auto presentLayoutList = std::move(presentLayoutListResult).value();
    assert(presentLayoutList.begin().ok());
    rhi::BarrierBatch presentLayoutBatch;
    rhi::TextureBarrier presentLayoutBarrier;
    presentLayoutBarrier.texture = layoutImage.image;
    presentLayoutBarrier.oldLayout = rhi::TextureLayout::color_attachment;
    presentLayoutBarrier.newLayout = rhi::TextureLayout::present;
    presentLayoutBarrier.sourceStages =
        rhi::PipelineStage::color_attachment_output;
    presentLayoutBarrier.sourceAccess = rhi::Access::color_attachment_write;
    presentLayoutBatch.textures.push_back(presentLayoutBarrier);
    assert(presentLayoutList.barrier(presentLayoutBatch).ok());
    assert(presentLayoutList.end().ok());
    std::array<rhi::CommandList*, 1> presentLayoutLists{&presentLayoutList};
    assert(queue.submit(presentLayoutLists).ok());
    assert(queue.present(swapchain, layoutImage.imageIndex).ok());

    // Timeline semaphore values order submissions across queue kinds, while
    // barrier batches track queue ownership and resource state explicitly.
    auto timelineResult = context.device.create_semaphore();
    assert(timelineResult.ok());
    auto timeline = std::move(timelineResult).value();
    auto syncBuffer = tests::make_buffer(
        context.device, 64,
        rhi::BufferUsage::storage | rhi::BufferUsage::copy_destination);
    auto transferPoolResult =
        context.device.create_command_pool(rhi::QueueKind::transfer);
    assert(transferPoolResult.ok());
    auto transferPool = std::move(transferPoolResult).value();
    auto releaseListResult = transferPool.allocate();
    assert(releaseListResult.ok());
    auto releaseList = std::move(releaseListResult).value();
    assert(releaseList.begin().ok());
    rhi::BarrierBatch releaseBatch;
    releaseBatch.buffers.push_back({
        .buffer = &syncBuffer,
        .sourceStages = rhi::PipelineStage::copy,
        .destinationStages = rhi::PipelineStage::bottom,
        .sourceAccess = rhi::Access::transfer_write,
        .transferOwnership = true,
        .sourceQueue = rhi::QueueKind::transfer,
        .destinationQueue = rhi::QueueKind::graphics,
    });
    assert(releaseList.barrier(releaseBatch).ok());
    assert(releaseList.end().ok());
    auto transferQueueResult = context.device.queue(rhi::QueueKind::transfer);
    assert(transferQueueResult.ok());
    auto transferQueue = std::move(transferQueueResult).value();
    std::array<rhi::CommandList*, 1> releaseLists{&releaseList};
    const std::array<rhi::SemaphoreSignal, 1> releaseSignals{{
        {.semaphore = &timeline, .value = 5},
    }};
    assert(transferQueue
               .submit({.commandLists = releaseLists,
                        .signals = releaseSignals})
               .ok());
    assert(timeline.value() == 5);

    auto acquireListResult = pool.allocate();
    assert(acquireListResult.ok());
    auto acquireList = std::move(acquireListResult).value();
    assert(acquireList.begin().ok());
    rhi::BarrierBatch acquireBatch;
    acquireBatch.buffers.push_back({
        .buffer = &syncBuffer,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::vertex_input,
        .destinationAccess = rhi::Access::vertex_attribute_read,
    });
    acquireBatch.aliasing.push_back({
        .beforeBuffer = &replacementBuffer,
        .afterBuffer = &syncBuffer,
    });
    assert(acquireList.barrier(acquireBatch).ok());
    assert(acquireList.end().ok());
    std::array<rhi::CommandList*, 1> acquireLists{&acquireList};
    const std::array<rhi::SemaphoreWait, 1> acquireWaits{{
        {.semaphore = &timeline,
         .value = 5,
         .stages = rhi::PipelineStage::vertex_input},
    }};
    auto syncFenceResult = context.device.create_fence();
    assert(syncFenceResult.ok());
    auto syncFence = std::move(syncFenceResult).value();
    assert(queue
               .submit({.commandLists = acquireLists,
                        .waits = acquireWaits,
                        .signalFence = &syncFence,
                        .signalFenceValue = 9})
               .ok());
    assert(syncFence.completed_value() == 9);

    auto timeoutSemaphoreResult = context.device.create_semaphore();
    assert(timeoutSemaphoreResult.ok());
    auto timeoutSemaphore = std::move(timeoutSemaphoreResult).value();
    auto timeoutListResult = pool.allocate();
    assert(timeoutListResult.ok());
    auto timeoutList = std::move(timeoutListResult).value();
    assert(timeoutList.begin().ok());
    assert(timeoutList.end().ok());
    std::array<rhi::CommandList*, 1> timeoutLists{&timeoutList};
    const std::array<rhi::SemaphoreWait, 1> timeoutWaits{{
        {.semaphore = &timeoutSemaphore, .value = 1},
    }};
    const auto timeoutStatus = queue.submit({
        .commandLists = timeoutLists,
        .waits = timeoutWaits,
        .waitTimeout = std::chrono::nanoseconds{1},
    });
    assert(timeoutStatus.code == rhi::StatusCode::timeout);
    assert(timeoutList.state() == rhi::CommandListState::executable);

    auto timestampPoolResult = context.device.create_query_pool({
        .type = rhi::QueryType::timestamp,
        .count = 2,
    });
    auto occlusionPoolResult = context.device.create_query_pool({
        .type = rhi::QueryType::occlusion,
        .count = 1,
    });
    auto queryReadbackResult = context.device.create_buffer({
        .size = 3 * sizeof(std::uint64_t),
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(timestampPoolResult.ok() && occlusionPoolResult.ok() &&
           queryReadbackResult.ok());
    auto timestampPool = std::move(timestampPoolResult).value();
    auto occlusionPool = std::move(occlusionPoolResult).value();
    auto queryReadback = std::move(queryReadbackResult).value();
    auto queryListResult = pool.allocate();
    assert(queryListResult.ok());
    auto queryList = std::move(queryListResult).value();
    assert(queryList.begin().ok());
    assert(queryList.write_timestamp(timestampPool, 0).ok());
    assert(!queryList.write_timestamp(timestampPool, 2).ok());
    auto queryRenderResult = queryList.begin_rendering({.extent = {1, 1}});
    assert(queryRenderResult.ok());
    auto queryRender = std::move(queryRenderResult).value();
    assert(queryRender.begin_occlusion_query(occlusionPool, 0).ok());
    assert(queryRender.end_occlusion_query().ok());
    assert(queryRender.end().ok());
    assert(queryList.write_timestamp(timestampPool, 1).ok());
    assert(queryList.resolve_queries(timestampPool, 0, 2, queryReadback).ok());
    assert(queryList
               .resolve_queries(occlusionPool, 0, 1, queryReadback,
                                2 * sizeof(std::uint64_t))
               .ok());
    assert(queryList.end().ok());
    std::array<rhi::CommandList*, 1> queryLists{&queryList};
    assert(queue.submit(queryLists).ok());
    std::array<std::uint64_t, 3> queryResults{};
    assert(queryReadback
               .read(0, std::as_writable_bytes(std::span{queryResults}))
               .ok());
    assert((queryResults == std::array<std::uint64_t, 3>{1, 2, 1}));

    auto syncTextureResult = context.device.create_texture({
        .extent = {4, 4, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled |
                 rhi::TextureUsage::copy_destination,
        .mipLevels = 2,
    });
    assert(syncTextureResult.ok());
    auto syncTexture = std::move(syncTextureResult).value();
    auto textureBarrierListResult = pool.allocate();
    assert(textureBarrierListResult.ok());
    auto textureBarrierList = std::move(textureBarrierListResult).value();
    assert(textureBarrierList.begin().ok());
    rhi::BarrierBatch textureBatch;
    textureBatch.textures.push_back({
        .texture = &syncTexture,
        .range = {.aspects = rhi::TextureAspect::color,
                  .baseMipLevel = 1,
                  .mipLevelCount = 1,
                  .baseArrayLayer = 0,
                  .arrayLayerCount = 1},
        .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::shader_read_only,
        .destinationStages = rhi::PipelineStage::fragment_shader,
        .destinationAccess = rhi::Access::shader_read,
    });
    assert(textureBarrierList.barrier(textureBatch).ok());
    assert(textureBarrierList.end().ok());
    std::array<rhi::CommandList*, 1> textureBarrierLists{&textureBarrierList};
    assert(queue.submit(textureBarrierLists).ok());

    auto staleLayoutListResult = pool.allocate();
    assert(staleLayoutListResult.ok());
    auto staleLayoutList = std::move(staleLayoutListResult).value();
    assert(staleLayoutList.begin().ok());
    rhi::BarrierBatch staleLayoutBatch;
    staleLayoutBatch.textures.push_back({
        .texture = &syncTexture,
        .range = {.aspects = rhi::TextureAspect::color,
                  .baseMipLevel = 1,
                  .mipLevelCount = 1,
                  .baseArrayLayer = 0,
                  .arrayLayerCount = 1},
        .oldLayout = rhi::TextureLayout::transfer_source,
        .newLayout = rhi::TextureLayout::general,
    });
    assert(staleLayoutList.barrier(staleLayoutBatch).ok());
    assert(staleLayoutList.end().ok());
    std::array<rhi::CommandList*, 1> staleLayoutLists{&staleLayoutList};
    assert(queue.submit(staleLayoutLists).code ==
           rhi::StatusCode::invalid_state);

    auto ringResult = context.device.create_upload_ring(2, 256);
    assert(ringResult.ok());
    auto ring = std::move(ringResult).value();
    auto allocation = ring.allocate(32, 16);
    assert(allocation.valid());
    assert(allocation.offset == 0);
    assert(ring.advance().ok());
    assert(ring.allocate(256).valid());
    assert(!ring.allocate(1).valid());

    // Resource creation is device-thread-safe, while command pools retain
    // explicit thread ownership.
    std::atomic<std::uint32_t> created{0};
    std::vector<std::thread> workers;
    workers.reserve(4);
    for (std::uint32_t worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&context, &created] {
            auto concurrentBuffer = context.device.create_buffer({
                .size = 16,
                .usage = rhi::BufferUsage::storage,
            });
            if (concurrentBuffer.ok()) {
                ++created;
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    assert(created == 4);

    rhi::StatusCode crossThreadCode = rhi::StatusCode::ok;
    std::thread foreignPoolUser{[&pool, &crossThreadCode] {
        const auto foreignList = pool.allocate();
        crossThreadCode = foreignList.status().code;
    }};
    foreignPoolUser.join();
    assert(crossThreadCode == rhi::StatusCode::invalid_state);

    auto foreignContext = tests::make_null_context();
    auto foreignBuffer = tests::make_buffer(
        foreignContext.device, 64, rhi::BufferUsage::vertex);
    assert(list.reset().ok());
    assert(list.begin().ok());
    auto foreignEncoderResult = list.begin_rendering({.extent = {8, 8}});
    assert(foreignEncoderResult.ok());
    auto foreignEncoder = std::move(foreignEncoderResult).value();
    assert(foreignEncoder.bind_pipeline(pipeline).ok());
    const auto foreignBind = foreignEncoder.bind_vertex_buffer(0, foreignBuffer);
    assert(foreignBind.code == rhi::StatusCode::invalid_argument);
    assert(foreignEncoder.end().ok());
    assert(list.end().ok());

    std::vector<std::byte> shaderCode{std::byte{1}};
    auto foreignShaderResult = foreignContext.device.create_shader({
        .stage = rhi::ShaderStage::compute,
        .code = shaderCode,
    });
    assert(foreignShaderResult.ok());
    auto foreignShader = std::move(foreignShaderResult).value();
    const auto foreignPipeline = context.device.create_compute_pipeline({
        .computeShader = &foreignShader,
    });
    assert(!foreignPipeline.ok());
    assert(foreignPipeline.status().code == rhi::StatusCode::invalid_argument);

    const auto stats = context.instance.stats();
    assert(stats.devicesCreated == 1);
    assert(stats.commandPoolsCreated == 2);
    assert(stats.commandListsCreated == 9);
    assert(stats.drawsRecorded == 1);
    assert(stats.submissions == 7);
    assert(stats.presentations == 2);
    return 0;
}

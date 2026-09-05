#include "truffle/rhi/vulkan_backend.hpp"
#include "truffle/rhi/shader_package.hpp"

#include "native_backend_smoke.hpp"
#include "vulkan_backend_test.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

#if defined(TRUFFLE_VULKAN_SHADER_PACKAGE_PATH)
[[nodiscard]] std::vector<std::byte> read_shader_package(const char* path) {
    std::ifstream input{path, std::ios::binary};
    assert(input);
    const std::vector<char> contents{std::istreambuf_iterator<char>{input}, {}};
    std::vector<std::byte> bytes(contents.size());
    std::transform(contents.begin(), contents.end(), bytes.begin(),
                   [](char value) {
                       return std::byte{static_cast<unsigned char>(value)};
                   });
    return bytes;
}
#endif

void verify_vulkan_buffers() {
    namespace rhi = truffle::rhi;

    auto instanceResult =
        rhi::create_vulkan_instance({.enableValidation = true});
    assert(instanceResult.ok());
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    const auto info = adapter.info();
    assert(info.backend == rhi::BackendKind::vulkan);
    assert(info.platform == rhi::PlatformKind::linux_host);
    assert(info.maturity == rhi::BackendMaturity::native_smoke);
    assert(info.native && !info.validationOnly);
    assert(info.resources.bufferViews);
    assert(info.resources.bufferCopy);
    assert(info.resources.bufferFill);
    assert(info.resources.textureViews);
    assert(info.resources.bufferTextureCopy);
    assert(info.resources.textureCopy);
    assert(info.resources.textureClear);
    assert(info.resources.textureResolve);
    assert(info.resources.textureBlitNearest);
    assert(info.resources.textureBlitLinear);
    assert(info.pipelines.multipleRenderTargets);
    assert(info.pipelines.depthStencil);
    assert(info.pipelines.multisample);
    assert(!info.bindings.bindlessTables);
    assert(!info.bindings.updateAfterBind);
    assert(!info.pipelines.indirectCount);
    assert(!info.pipelines.pipelineCache);
    assert(info.pipelines.maxColorAttachments >= 2);
    assert(info.timestampPeriodNanoseconds > 0.0);
    assert(std::find(info.supportedFeatures.begin(),
                     info.supportedFeatures.end(),
                     rhi::Feature::timestamp_queries) !=
           info.supportedFeatures.end());

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer,
                             rhi::Feature::compute,
                             rhi::Feature::memory_budget,
                             rhi::Feature::timestamp_queries},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    assert(device.memory_budget(rhi::MemoryDomain::device_local).ok());
    auto updateAfterBindArena = device.create_descriptor_arena({
        .updateAfterBind = true,
    });
    assert(!updateAfterBindArena.ok());
    assert(updateAfterBindArena.status().code == rhi::StatusCode::unsupported);
    auto unsupportedPipelineCache = device.create_pipeline_cache({});
    assert(!unsupportedPipelineCache.ok());
    assert(unsupportedPipelineCache.status().code ==
           rhi::StatusCode::unsupported);

#if defined(TRUFFLE_VULKAN_SHADER_PACKAGE_PATH)
    const auto packageBytes =
        read_shader_package(TRUFFLE_VULKAN_SHADER_PACKAGE_PATH);
    auto packageResult = rhi::ShaderPackage::load(packageBytes);
    assert(packageResult.ok());
    auto shaderResult = device.create_shader(
        packageResult.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::vertex);
    assert(shaderResult.ok());
    auto vertexShader = std::move(shaderResult).value();

#if defined(TRUFFLE_VULKAN_FRAGMENT_PACKAGE_PATH)
    const auto fragmentPackageBytes =
        read_shader_package(TRUFFLE_VULKAN_FRAGMENT_PACKAGE_PATH);
    auto fragmentPackageResult =
        rhi::ShaderPackage::load(fragmentPackageBytes);
    assert(fragmentPackageResult.ok());
    auto fragmentShaderResult = device.create_shader(
        fragmentPackageResult.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::fragment);
    assert(fragmentShaderResult.ok());
    auto fragmentShader = std::move(fragmentShaderResult).value();
    auto trianglePipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(trianglePipelineResult.ok());
    auto trianglePipeline = std::move(trianglePipelineResult).value();
    constexpr std::uint32_t triangleSize = 16;
    constexpr std::size_t triangleRowPitch = 256;
    auto triangleTargetResult = device.create_texture({
        .extent = {triangleSize, triangleSize, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment |
                 rhi::TextureUsage::copy_source,
    });
    auto triangleReadbackResult = device.create_buffer({
        .size = triangleRowPitch * triangleSize,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(triangleTargetResult.ok() && triangleReadbackResult.ok());
    auto triangleTarget = std::move(triangleTargetResult).value();
    auto triangleReadback = std::move(triangleReadbackResult).value();
    auto trianglePoolResult =
        device.create_command_pool(rhi::QueueKind::graphics);
    assert(trianglePoolResult.ok());
    auto trianglePool = std::move(trianglePoolResult).value();
    auto triangleListResult = trianglePool.allocate();
    assert(triangleListResult.ok());
    auto triangleList = std::move(triangleListResult).value();
    assert(triangleList.begin().ok());
    auto triangleRenderResult = triangleList.begin_rendering({
        .extent = {triangleSize, triangleSize},
        .colorAttachments = {{.texture = &triangleTarget,
                              .clear = {0.0F, 0.0F, 0.0F, 1.0F}}},
    });
    assert(triangleRenderResult.ok());
    auto triangleRender = std::move(triangleRenderResult).value();
    assert(triangleRender.bind_pipeline(trianglePipeline).ok());
    assert(triangleRender.draw(3).ok());
    assert(triangleRender.end().ok());
    auto triangleCopyResult = triangleList.begin_copy();
    assert(triangleCopyResult.ok());
    auto triangleCopy = std::move(triangleCopyResult).value();
    assert(triangleCopy
               .copy_texture_to_buffer(
                   triangleTarget, triangleReadback,
                   {.layout = {.bytesPerRow = triangleRowPitch,
                               .rowsPerImage = triangleSize},
                    .texture = {.extent = {triangleSize, triangleSize, 1}}})
               .ok());
    assert(triangleCopy.end().ok());
    assert(triangleList.end().ok());
    auto triangleQueueResult = device.queue(rhi::QueueKind::graphics);
    assert(triangleQueueResult.ok());
    auto triangleQueue = std::move(triangleQueueResult).value();
    std::array<rhi::CommandList*, 1> triangleLists{&triangleList};
    assert(triangleQueue.submit(triangleLists).ok());
    std::array<std::byte, 4> trianglePixel{};
    assert(triangleReadback
               .read((triangleSize / 2) * triangleRowPitch +
                         (triangleSize / 2) * 4,
                     trianglePixel)
               .ok());
    const std::array<std::byte, 4> expectedTrianglePixel{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    assert(trianglePixel == expectedTrianglePixel);

    constexpr std::size_t synchronizedBytes = 64;
    std::array<std::byte, synchronizedBytes> synchronizedExpected{};
    for (std::size_t index = 0; index < synchronizedExpected.size(); ++index) {
        synchronizedExpected[index] =
            std::byte{static_cast<unsigned char>(index * 5u + 7u)};
    }
    auto synchronizedUpload = device.create_buffer({
        .size = synchronizedBytes,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto synchronizedIntermediate = device.create_buffer({
        .size = synchronizedBytes,
        .usage = rhi::BufferUsage::copy_source |
                 rhi::BufferUsage::copy_destination,
    });
    auto synchronizedReadback = device.create_buffer({
        .size = synchronizedBytes,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(synchronizedUpload.ok() && synchronizedIntermediate.ok() &&
           synchronizedReadback.ok());
    assert(synchronizedUpload.value()
               .write(0, std::as_bytes(std::span{synchronizedExpected}))
               .ok());
    auto transferQueue = device.queue(rhi::QueueKind::transfer);
    auto transferPool = device.create_command_pool(rhi::QueueKind::transfer);
    auto synchronizationComputeQueue = device.queue(rhi::QueueKind::compute);
    assert(transferQueue.ok() && transferPool.ok() &&
           synchronizationComputeQueue.ok());
    auto firstSynchronized = transferPool.value().allocate();
    auto secondSynchronized = trianglePool.allocate();
    auto trailingSynchronized = trianglePool.allocate();
    assert(firstSynchronized.ok() && secondSynchronized.ok() &&
           trailingSynchronized.ok());
    assert(firstSynchronized.value().begin().ok());
    auto firstSynchronizedCopy = firstSynchronized.value().begin_copy();
    assert(firstSynchronizedCopy.ok());
    assert(firstSynchronizedCopy.value()
               .copy_buffer(synchronizedUpload.value(), 0,
                            synchronizedIntermediate.value(), 0,
                            synchronizedBytes)
               .ok());
    assert(firstSynchronizedCopy.value().end().ok());
    rhi::BarrierBatch copyBarrier;
    copyBarrier.buffers.push_back({
        .buffer = &synchronizedIntermediate.value(),
        .sourceStages = rhi::PipelineStage::copy,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = rhi::Access::transfer_write,
        .destinationAccess = rhi::Access::transfer_read,
        .transferOwnership = true,
        .sourceQueue = rhi::QueueKind::transfer,
        .destinationQueue = rhi::QueueKind::graphics,
    });
    assert(firstSynchronized.value().barrier(copyBarrier).ok());
    assert(firstSynchronized.value().end().ok());
    assert(secondSynchronized.value().begin().ok());
    auto secondSynchronizedCopy = secondSynchronized.value().begin_copy();
    assert(secondSynchronizedCopy.ok());
    assert(secondSynchronizedCopy.value()
               .copy_buffer(synchronizedIntermediate.value(), 0,
                            synchronizedReadback.value(), 0,
                            synchronizedBytes)
               .ok());
    assert(secondSynchronizedCopy.value().end().ok());
    assert(secondSynchronized.value().end().ok());
    assert(trailingSynchronized.value().begin().ok());
    assert(trailingSynchronized.value().end().ok());
    auto timeline = device.create_semaphore();
    auto completion = device.create_fence();
    assert(timeline.ok() && completion.ok());
    std::array<rhi::CommandList*, 1> transferLists{
        &firstSynchronized.value()};
    const std::array<rhi::SemaphoreSignal, 1> transferSignal{{
        {.semaphore = &timeline.value(), .value = 3},
    }};
    assert(transferQueue.value()
               .submit({.commandLists = transferLists,
                        .signals = transferSignal})
               .ok());
    std::array<rhi::CommandList*, 2> synchronizedLists{
        &secondSynchronized.value(), &trailingSynchronized.value()};
    const std::array<rhi::SemaphoreWait, 1> synchronizedWait{{
        {.semaphore = &timeline.value(),
         .value = 3,
         .stages = rhi::PipelineStage::copy},
    }};
    const std::array<rhi::SemaphoreSignal, 1> synchronizedSignal{{
        {.semaphore = &timeline.value(), .value = 4},
    }};
    assert(triangleQueue
               .submit({.commandLists = synchronizedLists,
                        .waits = synchronizedWait,
                        .signals = synchronizedSignal,
                        .signalFence = &completion.value(),
                        .signalFenceValue = 7})
               .ok());
    assert(timeline.value().value() == 4);
    assert(completion.value().completed_value() == 7);
    assert(completion.value().wait(7, std::chrono::nanoseconds{1}).ok());
    const auto fenceTimeout =
        completion.value().wait(8, std::chrono::nanoseconds{1});
    assert(!fenceTimeout.ok());
    assert(fenceTimeout.code == rhi::StatusCode::timeout);
    std::array<std::byte, synchronizedBytes> synchronizedOutput{};
    assert(synchronizedReadback.value().read(0, synchronizedOutput).ok());
    assert(synchronizedOutput == synchronizedExpected);

    auto transitionedTexture = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled |
                 rhi::TextureUsage::copy_destination,
    });
    auto aliasBefore = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::copy_destination,
    });
    auto aliasAfter = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::copy_source,
    });
    assert(transitionedTexture.ok() && aliasBefore.ok() && aliasAfter.ok());
    auto explicitBarrierList = trianglePool.allocate();
    assert(explicitBarrierList.ok());
    assert(explicitBarrierList.value().begin().ok());
    rhi::BarrierBatch explicitBarriers;
    explicitBarriers.textures.push_back({
        .texture = &transitionedTexture.value(),
        .range = {.aspects = rhi::TextureAspect::color},
        .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::shader_read_only,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::fragment_shader,
        .sourceAccess = rhi::Access::none,
        .destinationAccess = rhi::Access::shader_read,
    });
    explicitBarriers.aliasing.push_back({
        .beforeBuffer = &aliasBefore.value(),
        .afterBuffer = &aliasAfter.value(),
        .sourceStages = rhi::PipelineStage::copy,
        .destinationStages = rhi::PipelineStage::copy,
    });
    assert(explicitBarrierList.value().barrier(explicitBarriers).ok());
    assert(explicitBarrierList.value().end().ok());
    std::array<rhi::CommandList*, 1> explicitBarrierLists{
        &explicitBarrierList.value()};
    assert(triangleQueue.submit(explicitBarrierLists).ok());

    auto blockedTimeline = device.create_semaphore();
    auto waitingList = trianglePool.allocate();
    assert(blockedTimeline.ok() && waitingList.ok());
    assert(waitingList.value().begin().ok());
    assert(waitingList.value().end().ok());
    std::array<rhi::CommandList*, 1> waitingLists{&waitingList.value()};
    const std::array<rhi::SemaphoreWait, 1> blockedWait{{
        {.semaphore = &blockedTimeline.value(), .value = 1},
    }};
    const auto waitTimeout = triangleQueue.submit({
        .commandLists = waitingLists,
        .waits = blockedWait,
        .waitTimeout = std::chrono::nanoseconds{1},
    });
    assert(!waitTimeout.ok());
    assert(waitTimeout.code == rhi::StatusCode::timeout);
    assert(waitingList.value().state() == rhi::CommandListState::executable);
    auto releaseList = trianglePool.allocate();
    assert(releaseList.ok());
    assert(releaseList.value().begin().ok());
    assert(releaseList.value().end().ok());
    std::array<rhi::CommandList*, 1> releaseLists{&releaseList.value()};
    const std::array<rhi::SemaphoreSignal, 1> releaseSignal{{
        {.semaphore = &blockedTimeline.value(), .value = 1},
    }};
    assert(triangleQueue
               .submit({.commandLists = releaseLists,
                        .signals = releaseSignal})
               .ok());
    assert(triangleQueue
               .submit({.commandLists = waitingLists,
                        .waits = blockedWait,
                        .waitTimeout = std::chrono::milliseconds{1}})
               .ok());

    auto timestamps = device.create_query_pool({
        .type = rhi::QueryType::timestamp,
        .count = 2,
    });
    auto occlusion = device.create_query_pool({
        .type = rhi::QueryType::occlusion,
        .count = 1,
    });
    auto queryReadback = device.create_buffer({
        .size = 3 * sizeof(std::uint64_t),
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(timestamps.ok() && occlusion.ok() && queryReadback.ok());
    auto unsupportedStatistics = device.create_query_pool({
        .type = rhi::QueryType::pipeline_statistics,
        .count = 1,
    });
    assert(!unsupportedStatistics.ok());
    assert(unsupportedStatistics.status().code == rhi::StatusCode::unsupported);
    auto queryList = trianglePool.allocate();
    assert(queryList.ok());
    assert(queryList.value().begin().ok());
    assert(queryList.value().write_timestamp(timestamps.value(), 0).ok());
    auto queryRender = queryList.value().begin_rendering({
        .extent = {triangleSize, triangleSize},
        .colorAttachments = {{.texture = &triangleTarget}},
    });
    assert(queryRender.ok());
    assert(queryRender.value()
               .begin_occlusion_query(occlusion.value(), 0)
               .ok());
    assert(queryRender.value().bind_pipeline(trianglePipeline).ok());
    assert(queryRender.value().draw(3).ok());
    assert(queryRender.value().end_occlusion_query().ok());
    assert(queryRender.value().end().ok());
    assert(queryList.value().write_timestamp(timestamps.value(), 1).ok());
    assert(queryList.value()
               .resolve_queries(timestamps.value(), 0, 2,
                                queryReadback.value())
               .ok());
    assert(queryList.value()
               .resolve_queries(occlusion.value(), 0, 1,
                                queryReadback.value(),
                                2 * sizeof(std::uint64_t))
               .ok());
    assert(queryList.value().end().ok());
    std::array<rhi::CommandList*, 1> queryLists{&queryList.value()};
    assert(triangleQueue.submit(queryLists).ok());
    std::array<std::uint64_t, 3> queryResults{};
    assert(queryReadback.value()
               .read(0, std::as_writable_bytes(std::span{queryResults}))
               .ok());
    assert(queryResults[0] != 0);
    assert(queryResults[1] >= queryResults[0]);
    assert(queryResults[2] > 0);
    assert(queryResults[2] <= triangleSize * triangleSize);

    const auto render_and_read = [&](rhi::Pipeline& pipeline, auto&& encode) {
        assert(triangleList.reset().ok());
        assert(triangleList.begin().ok());
        auto renderResult = triangleList.begin_rendering({
            .extent = {triangleSize, triangleSize},
            .colorAttachments = {{.texture = &triangleTarget,
                                  .clear = {0.0F, 0.0F, 0.0F, 1.0F}}},
        });
        assert(renderResult.ok());
        auto render = std::move(renderResult).value();
        assert(render.bind_pipeline(pipeline).ok());
        encode(render);
        assert(render.end().ok());
        auto copyResult = triangleList.begin_copy();
        assert(copyResult.ok());
        auto copy = std::move(copyResult).value();
        assert(copy
                   .copy_texture_to_buffer(
                       triangleTarget, triangleReadback,
                       {.layout = {.bytesPerRow = triangleRowPitch,
                                   .rowsPerImage = triangleSize},
                        .texture = {
                            .extent = {triangleSize, triangleSize, 1}}})
                   .ok());
        assert(copy.end().ok());
        assert(triangleList.end().ok());
        std::array<rhi::CommandList*, 1> lists{&triangleList};
        assert(triangleQueue.submit(lists).ok());
        std::array<std::byte, 4> pixel{};
        assert(triangleReadback
                   .read((triangleSize / 2) * triangleRowPitch +
                             (triangleSize / 2) * 4,
                         pixel)
                   .ok());
        return pixel;
    };

    auto indexBufferResult = device.create_buffer({
        .size = 3 * sizeof(std::uint16_t),
        .usage = rhi::BufferUsage::index,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(indexBufferResult.ok());
    auto indexBuffer = std::move(indexBufferResult).value();
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    assert(indexBuffer.write(0, std::as_bytes(std::span{indices})).ok());
    assert(render_and_read(trianglePipeline, [&](rhi::RenderEncoder& render) {
               assert(render
                          .bind_index_buffer(indexBuffer, 0,
                                             rhi::IndexFormat::uint16)
                          .ok());
               assert(render.draw_indexed(3, 2).ok());
           }) == expectedTrianglePixel);

    auto indirectBufferResult = device.create_buffer({
        .size = 4 * sizeof(std::uint32_t),
        .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(indirectBufferResult.ok());
    auto indirectBuffer = std::move(indirectBufferResult).value();
    const std::array<std::uint32_t, 4> indirectCommand{3, 1, 0, 0};
    assert(indirectBuffer
               .write(0, std::as_bytes(std::span{indirectCommand}))
               .ok());
    assert(render_and_read(trianglePipeline, [&](rhi::RenderEncoder& render) {
               assert(render
                          .draw_indirect(indirectBuffer, 0, false, 1,
                                         4 * sizeof(std::uint32_t))
                          .ok());
           }) == expectedTrianglePixel);
    auto indirectCountBufferResult = device.create_buffer({
        .size = sizeof(std::uint32_t),
        .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(indirectCountBufferResult.ok());
    auto indirectCountBuffer = std::move(indirectCountBufferResult).value();
    const std::array<std::uint32_t, 1> indirectCount{1};
    assert(indirectCountBuffer
               .write(0, std::as_bytes(std::span{indirectCount}))
               .ok());
    assert(render_and_read(trianglePipeline, [&](rhi::RenderEncoder& render) {
               const auto status = render.draw_indirect_count(
                   indirectBuffer, 0, indirectCountBuffer, 0, 1,
                   4 * sizeof(std::uint32_t), false);
               assert(!status.ok());
               assert(status.code == rhi::StatusCode::unsupported);
               assert(render.draw(3).ok());
           }) == expectedTrianglePixel);

    const std::array<std::byte, 4> greenPixel{
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
#if defined(TRUFFLE_VULKAN_TEXTURED_FRAGMENT_PACKAGE_PATH)
    const auto texturedPackageBytes =
        read_shader_package(TRUFFLE_VULKAN_TEXTURED_FRAGMENT_PACKAGE_PATH);
    auto texturedPackageResult =
        rhi::ShaderPackage::load(texturedPackageBytes);
    assert(texturedPackageResult.ok());
    auto texturedDesc = texturedPackageResult.value().desc();
    texturedDesc.variants[0].reflection.bindings = {
        {.name = "sourceTexture",
         .stage = rhi::ShaderStage::fragment,
         .type = rhi::ResourceBindingType::texture,
         .group = 0,
         .binding = 0},
        {.name = "sourceSampler",
         .stage = rhi::ShaderStage::fragment,
         .type = rhi::ResourceBindingType::sampler,
         .group = 0,
         .binding = 1},
    };
    auto reflectedTexturedPackage =
        rhi::ShaderPackage::create(std::move(texturedDesc));
    assert(reflectedTexturedPackage.ok());
    auto texturedShaderResult = device.create_shader(
        reflectedTexturedPackage.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::fragment);
    assert(texturedShaderResult.ok());
    auto texturedShader = std::move(texturedShaderResult).value();
    auto sampledLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {
            {.binding = 0,
             .type = rhi::BindingType::sampled_texture,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 1,
             .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment},
        },
    });
    assert(sampledLayoutResult.ok());
    auto sampledLayout = std::move(sampledLayoutResult).value();
    auto texturedPipelineLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&sampledLayout},
    });
    assert(texturedPipelineLayoutResult.ok());
    auto texturedPipelineLayout =
        std::move(texturedPipelineLayoutResult).value();
    auto texturedPipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &texturedShader,
        .layout = &texturedPipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(texturedPipelineResult.ok());
    auto texturedPipeline = std::move(texturedPipelineResult).value();
    auto sampledTextureResult = device.create_texture({
        .extent = {1, 1, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled |
                 rhi::TextureUsage::copy_destination,
    });
    auto sampledUploadResult = device.create_buffer({
        .size = 4,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(sampledTextureResult.ok() && sampledUploadResult.ok());
    auto sampledTexture = std::move(sampledTextureResult).value();
    auto sampledUpload = std::move(sampledUploadResult).value();
    assert(sampledUpload.write(0, greenPixel).ok());
    assert(triangleList.reset().ok());
    assert(triangleList.begin().ok());
    auto uploadCopyResult = triangleList.begin_copy();
    assert(uploadCopyResult.ok());
    auto uploadCopy = std::move(uploadCopyResult).value();
    assert(uploadCopy
               .copy_buffer_to_texture(
                   sampledUpload, sampledTexture,
                   {.texture = {.extent = {1, 1, 1}}})
               .ok());
    assert(uploadCopy.end().ok());
    assert(triangleList.end().ok());
    std::array<rhi::CommandList*, 1> uploadLists{&triangleList};
    assert(triangleQueue.submit(uploadLists).ok());
    auto sampledViewResult = device.create_texture_view(sampledTexture);
    auto sampledSamplerResult = device.create_sampler({});
    auto sampledArenaResult = device.create_descriptor_arena();
    assert(sampledViewResult.ok() && sampledSamplerResult.ok() &&
           sampledArenaResult.ok());
    auto sampledView = std::move(sampledViewResult).value();
    auto sampledSampler = std::move(sampledSamplerResult).value();
    auto sampledArena = std::move(sampledArenaResult).value();
    auto sampledGroupResult = device.create_bind_group({
        .layout = &sampledLayout,
        .arena = &sampledArena,
        .entries = {
            {.binding = 0, .textureView = &sampledView},
            {.binding = 1, .sampler = &sampledSampler},
        },
    });
    assert(sampledGroupResult.ok());
    auto sampledGroup = std::move(sampledGroupResult).value();
    assert(render_and_read(texturedPipeline, [&](rhi::RenderEncoder& render) {
               assert(render.bind_group(0, sampledGroup).ok());
               assert(render.draw(3).ok());
           }) == greenPixel);
#endif

    auto depthPipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .depthStencil = {
            .format = rhi::TextureFormat::depth32_float,
            .depthWriteEnabled = true,
            .depthCompare = rhi::CompareOp::less,
        },
    });
    auto depthTargetResult = device.create_texture({
        .extent = {triangleSize, triangleSize, 1},
        .format = rhi::TextureFormat::depth32_float,
        .usage = rhi::TextureUsage::depth_stencil_attachment,
    });
    assert(depthPipelineResult.ok() && depthTargetResult.ok());
    auto depthPipeline = std::move(depthPipelineResult).value();
    auto depthTarget = std::move(depthTargetResult).value();
    const auto render_with_depth = [&](float clearDepth) {
        assert(triangleList.reset().ok());
        assert(triangleList.begin().ok());
        auto renderResult = triangleList.begin_rendering({
            .extent = {triangleSize, triangleSize},
            .colorAttachments = {{.texture = &triangleTarget,
                                  .clear = {0.0F, 0.0F, 0.0F, 1.0F}}},
            .depthStencilAttachment = {
                .texture = &depthTarget,
                .clearDepth = clearDepth,
            },
        });
        assert(renderResult.ok());
        auto render = std::move(renderResult).value();
        assert(render.bind_pipeline(depthPipeline).ok());
        assert(render.draw(3).ok());
        assert(render.end().ok());
        auto copyResult = triangleList.begin_copy();
        assert(copyResult.ok());
        auto copy = std::move(copyResult).value();
        assert(copy
                   .copy_texture_to_buffer(
                       triangleTarget, triangleReadback,
                       {.layout = {.bytesPerRow = triangleRowPitch,
                                   .rowsPerImage = triangleSize},
                        .texture = {
                            .extent = {triangleSize, triangleSize, 1}}})
                   .ok());
        assert(copy.end().ok());
        assert(triangleList.end().ok());
        std::array<rhi::CommandList*, 1> lists{&triangleList};
        assert(triangleQueue.submit(lists).ok());
        std::array<std::byte, 4> pixel{};
        assert(triangleReadback
                   .read((triangleSize / 2) * triangleRowPitch +
                             (triangleSize / 2) * 4,
                         pixel)
                   .ok());
        return pixel;
    };
    assert(render_with_depth(1.0F) == expectedTrianglePixel);
    const std::array<std::byte, 4> blackPixel{
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    assert(render_with_depth(0.0F) == blackPixel);

#if defined(TRUFFLE_VULKAN_MRT_FRAGMENT_PACKAGE_PATH)
    const auto mrtPackageBytes =
        read_shader_package(TRUFFLE_VULKAN_MRT_FRAGMENT_PACKAGE_PATH);
    auto mrtPackageResult = rhi::ShaderPackage::load(mrtPackageBytes);
    assert(mrtPackageResult.ok());
    auto mrtShaderResult = device.create_shader(
        mrtPackageResult.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::fragment);
    assert(mrtShaderResult.ok());
    auto mrtShader = std::move(mrtShaderResult).value();
    auto mrtPipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &mrtShader,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm},
                         {.format = rhi::TextureFormat::rgba8_unorm}},
    });
    auto secondTargetResult = device.create_texture({
        .extent = {triangleSize, triangleSize, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment |
                 rhi::TextureUsage::copy_source,
    });
    auto secondReadbackResult = device.create_buffer({
        .size = triangleRowPitch * triangleSize,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(mrtPipelineResult.ok() && secondTargetResult.ok() &&
           secondReadbackResult.ok());
    auto mrtPipeline = std::move(mrtPipelineResult).value();
    auto secondTarget = std::move(secondTargetResult).value();
    auto secondReadback = std::move(secondReadbackResult).value();
    assert(triangleList.reset().ok());
    assert(triangleList.begin().ok());
    auto mrtRenderResult = triangleList.begin_rendering({
        .extent = {triangleSize, triangleSize},
        .colorAttachments = {{.texture = &triangleTarget},
                             {.texture = &secondTarget}},
    });
    assert(mrtRenderResult.ok());
    auto mrtRender = std::move(mrtRenderResult).value();
    assert(mrtRender.bind_pipeline(mrtPipeline).ok());
    assert(mrtRender.draw(3).ok());
    assert(mrtRender.end().ok());
    auto mrtCopyResult = triangleList.begin_copy();
    assert(mrtCopyResult.ok());
    auto mrtCopy = std::move(mrtCopyResult).value();
    const rhi::BufferTextureCopyRegion mrtRegion{
        .layout = {.bytesPerRow = triangleRowPitch,
                   .rowsPerImage = triangleSize},
        .texture = {.extent = {triangleSize, triangleSize, 1}},
    };
    assert(mrtCopy
               .copy_texture_to_buffer(triangleTarget, triangleReadback,
                                       mrtRegion)
               .ok());
    assert(mrtCopy
               .copy_texture_to_buffer(secondTarget, secondReadback, mrtRegion)
               .ok());
    assert(mrtCopy.end().ok());
    assert(triangleList.end().ok());
    std::array<rhi::CommandList*, 1> mrtLists{&triangleList};
    assert(triangleQueue.submit(mrtLists).ok());
    std::array<std::byte, 4> firstMrtPixel{};
    std::array<std::byte, 4> secondMrtPixel{};
    const auto centerOffset = (triangleSize / 2) * triangleRowPitch +
                              (triangleSize / 2) * 4;
    assert(triangleReadback.read(centerOffset, firstMrtPixel).ok());
    assert(secondReadback.read(centerOffset, secondMrtPixel).ok());
    assert(firstMrtPixel == expectedTrianglePixel);
    assert(secondMrtPixel == greenPixel);
#endif

    auto multisampleTargetResult = device.create_texture({
        .extent = {triangleSize, triangleSize, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment,
        .sampleCount = 4,
    });
    auto resolveTargetResult = device.create_texture({
        .extent = {triangleSize, triangleSize, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment |
                 rhi::TextureUsage::copy_source,
    });
    auto multisamplePipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .multisample = {.sampleCount = 4},
    });
    assert(multisampleTargetResult.ok() && resolveTargetResult.ok() &&
           multisamplePipelineResult.ok());
    auto multisampleTarget = std::move(multisampleTargetResult).value();
    auto resolveTarget = std::move(resolveTargetResult).value();
    auto multisamplePipeline =
        std::move(multisamplePipelineResult).value();
    assert(triangleList.reset().ok());
    assert(triangleList.begin().ok());
    auto multisampleRenderResult = triangleList.begin_rendering({
        .extent = {triangleSize, triangleSize},
        .colorAttachments = {{.texture = &multisampleTarget,
                              .resolveTexture = &resolveTarget}},
    });
    assert(multisampleRenderResult.ok());
    auto multisampleRender = std::move(multisampleRenderResult).value();
    assert(multisampleRender.bind_pipeline(multisamplePipeline).ok());
    assert(multisampleRender.draw(3).ok());
    assert(multisampleRender.end().ok());
    auto multisampleCopyResult = triangleList.begin_copy();
    assert(multisampleCopyResult.ok());
    auto multisampleCopy = std::move(multisampleCopyResult).value();
    assert(multisampleCopy
               .copy_texture_to_buffer(
                   resolveTarget, triangleReadback,
                   {.layout = {.bytesPerRow = triangleRowPitch,
                               .rowsPerImage = triangleSize},
                    .texture = {.extent = {triangleSize, triangleSize, 1}}})
               .ok());
    assert(multisampleCopy.end().ok());
    assert(triangleList.end().ok());
    std::array<rhi::CommandList*, 1> multisampleLists{&triangleList};
    assert(triangleQueue.submit(multisampleLists).ok());
    std::array<std::byte, 4> multisamplePixel{};
    assert(triangleReadback
               .read((triangleSize / 2) * triangleRowPitch +
                         (triangleSize / 2) * 4,
                     multisamplePixel)
               .ok());
    assert(multisamplePixel == expectedTrianglePixel);
#endif

    auto wrongFormatShader = device.create_shader({
        .stage = rhi::ShaderStage::vertex,
        .format = rhi::ShaderByteFormat::native_source,
        .code = {std::byte{0x03}, std::byte{0x02}, std::byte{0x23},
                 std::byte{0x07}},
    });
    assert(!wrongFormatShader.ok());
    assert(wrongFormatShader.status().code == rhi::StatusCode::unsupported);
    auto malformedShader = device.create_shader({
        .stage = rhi::ShaderStage::vertex,
        .format = rhi::ShaderByteFormat::spirv,
        .code = {std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                 std::byte{0x00}},
    });
    assert(!malformedShader.ok());
    assert(malformedShader.status().code ==
           rhi::StatusCode::invalid_argument);
    auto partialWordShader = device.create_shader({
        .stage = rhi::ShaderStage::vertex,
        .format = rhi::ShaderByteFormat::spirv,
        .code = {std::byte{0x03}, std::byte{0x02}, std::byte{0x23}},
    });
    assert(!partialWordShader.ok());
    assert(partialWordShader.status().code ==
           rhi::StatusCode::invalid_argument);
#endif

#if defined(TRUFFLE_VULKAN_COMPUTE_PACKAGE_PATH)
    const auto generatedComputeBytes =
        read_shader_package(TRUFFLE_VULKAN_COMPUTE_PACKAGE_PATH);
    auto generatedComputeResult =
        rhi::ShaderPackage::load(generatedComputeBytes);
    assert(generatedComputeResult.ok());
    auto computeDesc = generatedComputeResult.value().desc();
    assert(computeDesc.variants.size() == 1);
    computeDesc.variants[0].reflection.bindings = {
        {.name = "outputData",
         .stage = rhi::ShaderStage::compute,
         .type = rhi::ResourceBindingType::buffer,
         .group = 0,
         .binding = 0,
         .minimumSize = 16,
         .readOnly = false},
    };
    computeDesc.variants[0].reflection.requiredWorkgroupSize = {1, 1, 1};
    computeDesc.variants[0].reflection.preferredWorkgroupSize = {4, 1, 1};
    auto reflectedComputeResult =
        rhi::ShaderPackage::create(std::move(computeDesc));
    assert(reflectedComputeResult.ok());
    auto computeShaderResult = device.create_shader(
        reflectedComputeResult.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::compute);
    assert(computeShaderResult.ok());
    auto computeShader = std::move(computeShaderResult).value();
    auto missingLayoutPipeline = device.create_compute_pipeline({
        .computeShader = &computeShader,
    });
    assert(!missingLayoutPipeline.ok());
    assert(missingLayoutPipeline.status().code ==
           rhi::StatusCode::invalid_argument);
    auto storageLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute,
                     .minimumBufferSize = 16}},
    });
    assert(storageLayoutResult.ok());
    auto storageLayout = std::move(storageLayoutResult).value();
    auto unsupportedBindlessTable = device.create_bindless_table({
        .layout = &storageLayout,
        .capacity = 16,
    });
    assert(!unsupportedBindlessTable.ok());
    assert(unsupportedBindlessTable.status().code ==
           rhi::StatusCode::unsupported);
    auto wrongTypeLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::sampler,
                     .visibility = rhi::ShaderStageMask::compute}},
    });
    assert(wrongTypeLayoutResult.ok());
    auto wrongTypeLayout = std::move(wrongTypeLayoutResult).value();
    auto mismatchedPipelineLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&wrongTypeLayout},
    });
    assert(mismatchedPipelineLayoutResult.ok());
    auto mismatchedPipelineLayout =
        std::move(mismatchedPipelineLayoutResult).value();
    auto mismatchedPipeline = device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &mismatchedPipelineLayout,
    });
    assert(!mismatchedPipeline.ok());
    assert(mismatchedPipeline.status().code ==
           rhi::StatusCode::invalid_argument);
    auto computeLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&storageLayout},
    });
    assert(computeLayoutResult.ok());
    auto computeLayout = std::move(computeLayoutResult).value();
    auto computePipelineResult = device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &computeLayout,
    });
    assert(computePipelineResult.ok());
    auto computePipeline = std::move(computePipelineResult).value();
    auto remappedDesc = reflectedComputeResult.value().desc();
    remappedDesc.remaps = {{
        .target = rhi::ShaderTarget::spirv,
        .stage = rhi::ShaderStage::compute,
        .group = 0,
        .binding = 0,
        .nativeGroup = 0,
        .nativeBinding = 1,
    }};
    auto remappedPackageResult =
        rhi::ShaderPackage::create(std::move(remappedDesc));
    assert(remappedPackageResult.ok());
    auto remappedShaderResult = device.create_shader(
        remappedPackageResult.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::compute);
    assert(remappedShaderResult.ok());
    auto remappedShader = std::move(remappedShaderResult).value();
    auto unsupportedRemappedPipeline = device.create_compute_pipeline({
        .computeShader = &remappedShader,
        .layout = &computeLayout,
    });
    assert(!unsupportedRemappedPipeline.ok());
    assert(unsupportedRemappedPipeline.status().code ==
           rhi::StatusCode::unsupported);
    assert(computePipeline.preferred_workgroup_size() ==
           rhi::Extent3D(4, 1, 1));
    auto storageResult = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::storage,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(storageResult.ok());
    auto storage = std::move(storageResult).value();
    auto arenaResult = device.create_descriptor_arena();
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    auto groupResult = device.create_bind_group({
        .layout = &storageLayout,
        .arena = &arena,
        .entries = {{.binding = 0, .buffer = &storage, .size = 16}},
    });
    assert(groupResult.ok());
    auto group = std::move(groupResult).value();
    auto computePoolResult =
        device.create_command_pool(rhi::QueueKind::compute);
    assert(computePoolResult.ok());
    auto computePool = std::move(computePoolResult).value();
    auto computeListResult = computePool.allocate();
    assert(computeListResult.ok());
    auto computeList = std::move(computeListResult).value();
    assert(computeList.begin().ok());
    auto computeEncoderResult = computeList.begin_compute();
    assert(computeEncoderResult.ok());
    auto compute = std::move(computeEncoderResult).value();
    assert(compute.bind_pipeline(computePipeline).ok());
    assert(compute.bind_group(0, group).ok());
    assert(compute.dispatch(4, 1, 1).ok());
    assert(compute.end().ok());
    assert(computeList.end().ok());
    auto computeQueueResult = device.queue(rhi::QueueKind::compute);
    assert(computeQueueResult.ok());
    auto computeQueue = std::move(computeQueueResult).value();
    std::array<rhi::CommandList*, 1> computeLists{&computeList};
    assert(computeQueue.submit(computeLists).ok());
    std::array<std::uint32_t, 4> computeOutput{};
    assert(storage.read(0, std::as_writable_bytes(std::span{computeOutput}))
               .ok());
    const std::array<std::uint32_t, 4> expectedCompute{
        0x10203040u, 0x11213141u, 0x12223242u, 0x13233343u};
    assert(computeOutput == expectedCompute);
    auto dispatchIndirectResult = device.create_buffer({
        .size = 3 * sizeof(std::uint32_t),
        .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(dispatchIndirectResult.ok());
    auto dispatchIndirect = std::move(dispatchIndirectResult).value();
    const std::array<std::uint32_t, 3> dispatchArguments{4, 1, 1};
    assert(dispatchIndirect
               .write(0, std::as_bytes(std::span{dispatchArguments}))
               .ok());
    std::array<std::uint32_t, 4> clearedCompute{};
    assert(storage
               .write(0, std::as_bytes(std::span{clearedCompute}))
               .ok());
    assert(computeList.reset().ok());
    assert(computeList.begin().ok());
    auto indirectComputeResult = computeList.begin_compute();
    assert(indirectComputeResult.ok());
    auto indirectCompute = std::move(indirectComputeResult).value();
    assert(indirectCompute.bind_pipeline(computePipeline).ok());
    assert(indirectCompute.bind_group(0, group).ok());
    assert(indirectCompute.dispatch_indirect(dispatchIndirect, 0).ok());
    assert(indirectCompute.end().ok());
    assert(computeList.end().ok());
    std::array<rhi::CommandList*, 1> indirectComputeLists{&computeList};
    assert(computeQueue.submit(indirectComputeLists).ok());
    std::array<std::uint32_t, 4> indirectComputeOutput{};
    assert(storage
               .read(0,
                     std::as_writable_bytes(std::span{indirectComputeOutput}))
               .ok());
    assert(indirectComputeOutput == expectedCompute);

    assert(storage
               .write(0, std::as_bytes(std::span{clearedCompute}))
               .ok());
    assert(computeList.reset().ok());
    assert(computeList.begin().ok());
    auto retainedComputeResult = computeList.begin_compute();
    assert(retainedComputeResult.ok());
    auto retainedCompute = std::move(retainedComputeResult).value();
    assert(retainedCompute.bind_pipeline(computePipeline).ok());
    assert(retainedCompute.bind_group(0, group).ok());
    assert(retainedCompute.dispatch(4, 1, 1).ok());
    assert(retainedCompute.end().ok());
    assert(computeList.end().ok());
    assert(arena.reset().ok());
    std::array<rhi::CommandList*, 1> retainedLists{&computeList};
    assert(computeQueue.submit(retainedLists).ok());
    std::array<std::uint32_t, 4> retainedOutput{};
    assert(storage
               .read(0, std::as_writable_bytes(std::span{retainedOutput}))
               .ok());
    assert(retainedOutput == expectedCompute);

    assert(computeList.reset().ok());
    assert(computeList.begin().ok());
    auto retiredComputeResult = computeList.begin_compute();
    assert(retiredComputeResult.ok());
    auto retiredCompute = std::move(retiredComputeResult).value();
    assert(retiredCompute.bind_pipeline(computePipeline).ok());
    const auto retiredBind = retiredCompute.bind_group(0, group);
    assert(!retiredBind.ok());
    assert(retiredBind.code == rhi::StatusCode::invalid_argument);
    auto replacementGroupResult = device.create_bind_group({
        .layout = &storageLayout,
        .arena = &arena,
        .entries = {{.binding = 0, .buffer = &storage, .size = 16}},
    });
    assert(replacementGroupResult.ok());
    auto replacementGroup = std::move(replacementGroupResult).value();
    assert(retiredCompute.bind_group(0, replacementGroup).ok());
    assert(retiredCompute.dispatch(4, 1, 1).ok());
    assert(retiredCompute.end().ok());
    assert(computeList.end().ok());
    std::array<rhi::CommandList*, 1> replacementLists{&computeList};
    assert(computeQueue.submit(replacementLists).ok());

#if defined(TRUFFLE_VULKAN_COMPUTE_COLOR_FRAGMENT_PACKAGE_PATH) &&            \
    defined(TRUFFLE_VULKAN_FRAGMENT_PACKAGE_PATH) &&                          \
    defined(TRUFFLE_VULKAN_SHADER_PACKAGE_PATH)
    const auto computeColorBytes =
        read_shader_package(TRUFFLE_VULKAN_COMPUTE_COLOR_FRAGMENT_PACKAGE_PATH);
    auto computeColorPackageResult =
        rhi::ShaderPackage::load(computeColorBytes);
    assert(computeColorPackageResult.ok());
    auto computeColorDesc = computeColorPackageResult.value().desc();
    computeColorDesc.variants[0].reflection.bindings = {
        {.name = "inputData",
         .stage = rhi::ShaderStage::fragment,
         .type = rhi::ResourceBindingType::buffer,
         .group = 0,
         .binding = 0,
         .minimumSize = 16,
         .readOnly = true},
    };
    auto reflectedComputeColorPackage =
        rhi::ShaderPackage::create(std::move(computeColorDesc));
    assert(reflectedComputeColorPackage.ok());
    auto computeColorShaderResult = device.create_shader(
        reflectedComputeColorPackage.value(), rhi::ShaderTarget::spirv, "main",
        rhi::ShaderStage::fragment);
    assert(computeColorShaderResult.ok());
    auto computeColorShader = std::move(computeColorShaderResult).value();
    auto sharedStorageLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute |
                                   rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = 16}},
    });
    assert(sharedStorageLayoutResult.ok());
    auto sharedStorageLayout =
        std::move(sharedStorageLayoutResult).value();
    auto sharedPipelineLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&sharedStorageLayout},
    });
    assert(sharedPipelineLayoutResult.ok());
    auto sharedPipelineLayout =
        std::move(sharedPipelineLayoutResult).value();
    auto sharedComputePipelineResult = device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &sharedPipelineLayout,
    });
    auto computeColorPipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &computeColorShader,
        .layout = &sharedPipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    auto sharedArenaResult = device.create_descriptor_arena();
    assert(sharedComputePipelineResult.ok() &&
           computeColorPipelineResult.ok() && sharedArenaResult.ok());
    auto sharedComputePipeline =
        std::move(sharedComputePipelineResult).value();
    auto computeColorPipeline =
        std::move(computeColorPipelineResult).value();
    auto sharedArena = std::move(sharedArenaResult).value();
    auto sharedGroupResult = device.create_bind_group({
        .layout = &sharedStorageLayout,
        .arena = &sharedArena,
        .entries = {{.binding = 0, .buffer = &storage, .size = 16}},
    });
    assert(sharedGroupResult.ok());
    auto sharedGroup = std::move(sharedGroupResult).value();
    assert(storage
               .write(0, std::as_bytes(std::span{clearedCompute}))
               .ok());
    assert(triangleList.reset().ok());
    assert(triangleList.begin().ok());
    auto orderedComputeResult = triangleList.begin_compute();
    assert(orderedComputeResult.ok());
    auto orderedCompute = std::move(orderedComputeResult).value();
    assert(orderedCompute.bind_pipeline(sharedComputePipeline).ok());
    assert(orderedCompute.bind_group(0, sharedGroup).ok());
    assert(orderedCompute.dispatch(4, 1, 1).ok());
    assert(orderedCompute.end().ok());
    auto orderedRenderResult = triangleList.begin_rendering({
        .extent = {triangleSize, triangleSize},
        .colorAttachments = {{.texture = &triangleTarget,
                              .clear = {0.0F, 0.0F, 0.0F, 1.0F}}},
    });
    assert(orderedRenderResult.ok());
    auto orderedRender = std::move(orderedRenderResult).value();
    assert(orderedRender.bind_pipeline(computeColorPipeline).ok());
    assert(orderedRender.bind_group(0, sharedGroup).ok());
    assert(orderedRender.draw(3).ok());
    assert(orderedRender.end().ok());
    auto orderedCopyResult = triangleList.begin_copy();
    assert(orderedCopyResult.ok());
    auto orderedCopy = std::move(orderedCopyResult).value();
    assert(orderedCopy
               .copy_texture_to_buffer(
                   triangleTarget, triangleReadback,
                   {.layout = {.bytesPerRow = triangleRowPitch,
                               .rowsPerImage = triangleSize},
                    .texture = {.extent = {triangleSize, triangleSize, 1}}})
               .ok());
    assert(orderedCopy.end().ok());
    assert(triangleList.end().ok());
    std::array<rhi::CommandList*, 1> orderedLists{&triangleList};
    assert(triangleQueue.submit(orderedLists).ok());
    std::array<std::byte, 4> orderedPixel{};
    assert(triangleReadback
               .read((triangleSize / 2) * triangleRowPitch +
                         (triangleSize / 2) * 4,
                     orderedPixel)
               .ok());
    const std::array<std::byte, 4> expectedOrderedPixel{
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    assert(orderedPixel == expectedOrderedPixel);
#endif
#endif

    constexpr std::size_t byteCount = 64;
    auto uploadResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .mappedAtCreation = true,
        .debugName = "Vulkan upload proof",
    });
    auto deviceLocalResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source |
                 rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::device_local,
        .debugName = "Vulkan device-local proof",
    });
    auto readbackResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
        .debugName = "Vulkan readback proof",
    });
    assert(uploadResult.ok() && deviceLocalResult.ok() &&
           readbackResult.ok());
    auto upload = std::move(uploadResult).value();
    auto deviceLocal = std::move(deviceLocalResult).value();
    auto readback = std::move(readbackResult).value();

    auto uploadView = device.create_buffer_view(upload);
    assert(uploadView.ok());
    assert(uploadView.value().desc().size == byteCount);

    std::array<std::byte, byteCount> expected{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] =
            std::byte{static_cast<unsigned char>((index * 17u) ^ 0xa5u)};
    }
    assert(upload.write(0, expected).ok());
    assert(upload.unmap().ok());

    constexpr std::uint32_t textureWidth = 8;
    constexpr std::uint32_t textureHeight = 4;
    constexpr std::size_t textureRowPitch = 64;
    constexpr std::size_t textureBytes = textureRowPitch * textureHeight;
    auto textureUploadResult = device.create_buffer({
        .size = textureBytes,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto textureReadbackResult = device.create_buffer({
        .size = textureBytes,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    const rhi::TextureDesc textureDesc{
        .extent = {textureWidth, textureHeight, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    };
    auto sourceTextureResult = device.create_texture(textureDesc);
    auto destinationTextureResult = device.create_texture(textureDesc);
    assert(textureUploadResult.ok() && textureReadbackResult.ok() &&
           sourceTextureResult.ok() && destinationTextureResult.ok());
    auto textureUpload = std::move(textureUploadResult).value();
    auto textureReadback = std::move(textureReadbackResult).value();
    auto sourceTexture = std::move(sourceTextureResult).value();
    auto destinationTexture = std::move(destinationTextureResult).value();
    auto textureView = device.create_texture_view(sourceTexture);
    assert(textureView.ok());

    std::array<std::byte, textureBytes> expectedPixels{};
    for (std::size_t row = 0; row < textureHeight; ++row) {
        for (std::size_t columnByte = 0; columnByte < textureWidth * 4u;
             ++columnByte) {
            const auto index = row * textureRowPitch + columnByte;
            expectedPixels[index] =
                std::byte{static_cast<unsigned char>((index * 29u) ^ 0x3cu)};
        }
    }
    assert(textureUpload.write(0, expectedPixels).ok());

    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    auto encoderResult = list.begin_copy();
    assert(encoderResult.ok());
    auto encoder = std::move(encoderResult).value();
    assert(encoder.copy_buffer(upload, 0, deviceLocal, 0, byteCount).ok());
    assert(encoder.copy_buffer(deviceLocal, 0, readback, 0, byteCount).ok());
    assert(encoder.fill_buffer(readback, 32, 16, std::byte{0x7f}).ok());
    assert(encoder.fill_buffer(textureReadback, 0, textureBytes,
                               std::byte{0})
               .ok());
    const rhi::TextureRegion wholeTexture{
        .subresource = {.aspect = rhi::TextureAspect::color},
        .extent = {textureWidth, textureHeight, 1},
    };
    const rhi::BufferTextureCopyRegion textureRegion{
        .layout = {.bytesPerRow = textureRowPitch,
                   .rowsPerImage = textureHeight},
        .texture = wholeTexture,
    };
    assert(encoder.copy_buffer_to_texture(textureUpload, sourceTexture,
                                          textureRegion)
               .ok());
    assert(encoder.copy_texture(sourceTexture, destinationTexture,
                                {.source = wholeTexture,
                                 .destination = wholeTexture})
               .ok());
    assert(encoder.copy_texture_to_buffer(destinationTexture, textureReadback,
                                          textureRegion)
               .ok());
    assert(encoder.end().ok());
    assert(list.end().ok());

    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());

    std::fill_n(expected.begin() + 32, 16, std::byte{0x7f});
    std::array<std::byte, byteCount> output{};
    assert(readback.read(0, output).ok());
    assert(output == expected);

    auto mappedResult = readback.map();
    assert(mappedResult.ok());
    assert(readback.invalidate().ok());
    assert(std::ranges::equal(mappedResult.value(), expected));
    assert(readback.unmap().ok());

    std::array<std::byte, textureBytes> outputPixels{};
    assert(textureReadback.read(0, outputPixels).ok());
    assert(outputPixels == expectedPixels);

    const auto submit_copy = [&](auto&& record) {
        assert(list.reset().ok());
        assert(list.begin().ok());
        auto copyResult = list.begin_copy();
        assert(copyResult.ok());
        auto copy = std::move(copyResult).value();
        record(copy);
        assert(copy.end().ok());
        assert(list.end().ok());
        std::array<rhi::CommandList*, 1> copyLists{&list};
        assert(queue.submit(copyLists).ok());
    };
    const auto create_transfer_buffer = [&](std::size_t size,
                                            rhi::BufferUsage usage,
                                            rhi::MemoryDomain memory) {
        auto result = device.create_buffer({
            .size = size,
            .usage = usage,
            .memory = memory,
        });
        assert(result.ok());
        return std::move(result).value();
    };

    auto texture1d = device.create_texture({
        .dimension = rhi::TextureDimension::d1,
        .extent = {8, 1, 1},
        .format = rhi::TextureFormat::r8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
        .mipLevels = 4,
    });
    auto textureArray = device.create_texture({
        .extent = {8, 8, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
        .mipLevels = 4,
        .arrayLayers = 2,
    });
    auto texture3d = device.create_texture({
        .dimension = rhi::TextureDimension::d3,
        .extent = {2, 2, 2},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    auto cube = device.create_texture({
        .dimension = rhi::TextureDimension::cube,
        .extent = {4, 4, 1},
        .format = rhi::TextureFormat::rgba8_srgb,
        .usage = rhi::TextureUsage::sampled |
                 rhi::TextureUsage::copy_destination,
        .mipLevels = 3,
        .arrayLayers = 6,
    });
    assert(texture1d.ok() && textureArray.ok() && texture3d.ok() && cube.ok());
    auto cubeView = device.create_texture_view(
        cube.value(),
        {.dimension = rhi::TextureDimension::cube,
         .format = rhi::TextureFormat::rgba8_unorm,
         .range = {.mipLevelCount = 3, .arrayLayerCount = 6}});
    assert(cubeView.ok());
    auto invalidCubeView = device.create_texture_view(
        cube.value(),
        {.dimension = rhi::TextureDimension::cube,
         .format = rhi::TextureFormat::rgba8_unorm,
         .range = {.baseArrayLayer = 1, .arrayLayerCount = 5}});
    assert(!invalidCubeView.ok());
    assert(invalidCubeView.status().code == rhi::StatusCode::unsupported);

    auto hostMultisample = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .sampleCount = 4,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(!hostMultisample.ok());
    assert(hostMultisample.status().code == rhi::StatusCode::unsupported);

    assert(info.bindings.ordinaryBindGroups);
    assert(info.bindings.descriptorArrays);
    assert(info.bindings.dynamicOffsets);
    assert(info.bindings.immutableSamplers);
    assert(info.bindings.pushConstants);
    assert(info.pipelines.compute);
    assert(info.pipelines.graphics);
    assert(info.pipelines.indirect);
    assert(info.pipelines.maxColorAttachments >= 2);
    assert(info.pipelines.multipleRenderTargets);
    assert(info.pipelines.depthStencil);
    assert(info.pipelines.multisample);
    auto sampler = device.create_sampler({});
    assert(sampler.ok());
    auto anisotropicSampler = device.create_sampler({.maxAnisotropy = 2.0F});
    assert(!anisotropicSampler.ok());
    assert(anisotropicSampler.status().code == rhi::StatusCode::unsupported);

    std::array<std::byte, 16> mipLayerPixels{};
    for (std::size_t index = 0; index < mipLayerPixels.size(); ++index) {
        mipLayerPixels[index] =
            std::byte{static_cast<unsigned char>(0x30u + index)};
    }
    auto mipUpload = create_transfer_buffer(
        mipLayerPixels.size(), rhi::BufferUsage::copy_source,
        rhi::MemoryDomain::upload);
    auto mipReadback = create_transfer_buffer(
        mipLayerPixels.size(), rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(mipUpload.write(0, mipLayerPixels).ok());
    const rhi::TextureRegion mipLayerRegion{
        .subresource = {.mipLevel = 2, .arrayLayer = 1},
        .extent = {2, 2, 1},
    };
    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy
                   .copy_buffer_to_texture(
                       mipUpload, textureArray.value(),
                       {.texture = mipLayerRegion})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(
                       textureArray.value(), mipReadback,
                       {.texture = mipLayerRegion})
                   .ok());
    });
    std::array<std::byte, mipLayerPixels.size()> mipLayerOutput{};
    assert(mipReadback.read(0, mipLayerOutput).ok());
    assert(mipLayerOutput == mipLayerPixels);

    std::array<std::byte, 32> volumePixels{};
    for (std::size_t index = 0; index < volumePixels.size(); ++index) {
        volumePixels[index] =
            std::byte{static_cast<unsigned char>(0x80u + index)};
    }
    auto volumeUpload = create_transfer_buffer(
        volumePixels.size(), rhi::BufferUsage::copy_source,
        rhi::MemoryDomain::upload);
    auto volumeReadback = create_transfer_buffer(
        volumePixels.size(), rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(volumeUpload.write(0, volumePixels).ok());
    const rhi::TextureRegion volumeRegion{.extent = {2, 2, 2}};
    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy
                   .copy_buffer_to_texture(volumeUpload, texture3d.value(),
                                           {.texture = volumeRegion})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(texture3d.value(), volumeReadback,
                                           {.texture = volumeRegion})
                   .ok());
    });
    std::array<std::byte, volumePixels.size()> volumeOutput{};
    assert(volumeReadback.read(0, volumeOutput).ok());
    assert(volumeOutput == volumePixels);

    auto compressed = device.create_texture({
        .extent = {7, 7, 1},
        .format = rhi::TextureFormat::bc1_rgba_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    assert(compressed.ok());
    {
        std::array<std::byte, 32> compressedBytes{};
        for (std::size_t index = 0; index < compressedBytes.size(); ++index) {
            compressedBytes[index] =
                std::byte{static_cast<unsigned char>(index * 3u)};
        }
        auto compressedUpload = create_transfer_buffer(
            compressedBytes.size(), rhi::BufferUsage::copy_source,
            rhi::MemoryDomain::upload);
        auto compressedReadback = create_transfer_buffer(
            compressedBytes.size(), rhi::BufferUsage::copy_destination,
            rhi::MemoryDomain::readback);
        assert(compressedUpload.write(0, compressedBytes).ok());
        const rhi::TextureRegion compressedRegion{.extent = {7, 7, 1}};
        submit_copy([&](rhi::CopyEncoder& copy) {
            assert(copy
                       .copy_buffer_to_texture(
                           compressedUpload, compressed.value(),
                           {.texture = compressedRegion})
                       .ok());
            assert(copy
                       .copy_texture_to_buffer(
                           compressed.value(), compressedReadback,
                           {.texture = compressedRegion})
                       .ok());
        });
        std::array<std::byte, compressedBytes.size()> compressedOutput{};
        assert(compressedReadback.read(0, compressedOutput).ok());
        assert(compressedOutput == compressedBytes);
    }

    auto clearTexture = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    auto clearReadback = create_transfer_buffer(
        16, rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(clearTexture.ok());
    submit_copy([&](rhi::CopyEncoder& copy) {
        const rhi::TextureRegion region{.extent = {2, 2, 1}};
        assert(copy
                   .clear_texture(clearTexture.value(), region,
                                  {.color = {.r = 1.0F,
                                             .g = 0.5F,
                                             .b = 0.0F,
                                             .a = 1.0F}})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(clearTexture.value(), clearReadback,
                                           {.texture = region})
                   .ok());
    });
    std::array<std::byte, 16> clearOutput{};
    assert(clearReadback.read(0, clearOutput).ok());
    for (std::size_t offset = 0; offset < clearOutput.size(); offset += 4) {
        assert(clearOutput[offset] == std::byte{0xff});
        assert(clearOutput[offset + 1] == std::byte{0x80});
        assert(clearOutput[offset + 2] == std::byte{0x00});
        assert(clearOutput[offset + 3] == std::byte{0xff});
    }

    assert(list.reset().ok());
    assert(list.begin().ok());
    auto partialClearResult = list.begin_copy();
    assert(partialClearResult.ok());
    auto partialClear = std::move(partialClearResult).value();
    assert(partialClear
               .clear_texture(clearTexture.value(),
                              {.origin = {0, 0, 0}, .extent = {1, 2, 1}},
                              {.color = {.r = 1.0F}})
               .ok());
    assert(partialClear.end().ok());
    assert(list.end().ok());
    std::array<rhi::CommandList*, 1> partialClearLists{&list};
    const auto partialClearStatus = queue.submit(partialClearLists);
    assert(partialClearStatus.code == rhi::StatusCode::unsupported);
    assert(list.reset().ok());

    auto depthTexture = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::depth32_float,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::depth_stencil_attachment,
    });
    auto depthReadback = create_transfer_buffer(
        16, rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(depthTexture.ok());
    submit_copy([&](rhi::CopyEncoder& copy) {
        const rhi::TextureRegion region{
            .subresource = {.aspect = rhi::TextureAspect::depth},
            .extent = {2, 2, 1},
        };
        assert(copy
                   .clear_texture(depthTexture.value(), region,
                                  {.depth = 0.25F})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(depthTexture.value(), depthReadback,
                                           {.texture = region})
                   .ok());
    });
    std::array<std::byte, 16> depthOutput{};
    assert(depthReadback.read(0, depthOutput).ok());
    float depthValue = 0.0F;
    std::memcpy(&depthValue, depthOutput.data(), sizeof(depthValue));
    assert(depthValue == 0.25F);

    auto stencilTexture = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::depth24_unorm_stencil8,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::depth_stencil_attachment,
    });
    auto stencilReadback = create_transfer_buffer(
        16, rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(stencilTexture.ok());
    submit_copy([&](rhi::CopyEncoder& copy) {
        const rhi::TextureRegion region{
            .subresource = {.aspect = rhi::TextureAspect::stencil},
            .extent = {2, 2, 1},
        };
        assert(copy
                   .clear_texture(stencilTexture.value(), region,
                                  {.stencil = 0x5au})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(stencilTexture.value(),
                                           stencilReadback,
                                           {.texture = region})
                   .ok());
    });
    std::array<std::byte, 16> stencilOutput{};
    assert(stencilReadback.read(0, stencilOutput).ok());
    for (std::size_t index = 0; index < 4; ++index) {
        assert(stencilOutput[index] == std::byte{0x5a});
    }

    auto blitSource = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    auto blitDestination = device.create_texture({
        .extent = {4, 4, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    auto blitReadback = create_transfer_buffer(
        64, rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(blitSource.ok() && blitDestination.ok());
    submit_copy([&](rhi::CopyEncoder& copy) {
        const rhi::TextureRegion sourceRegion{.extent = {2, 2, 1}};
        const rhi::TextureRegion destinationRegion{.extent = {4, 4, 1}};
        assert(copy
                   .clear_texture(blitSource.value(), sourceRegion,
                                  {.color = {.r = 0.25F,
                                             .g = 0.5F,
                                             .b = 0.75F,
                                             .a = 1.0F}})
                   .ok());
        assert(copy
                   .blit_texture(blitSource.value(), blitDestination.value(),
                                 {.source = sourceRegion,
                                  .destination = destinationRegion,
                                  .filter = rhi::Filter::linear})
                   .ok());
        assert(copy
                   .copy_texture_to_buffer(blitDestination.value(), blitReadback,
                                           {.texture = destinationRegion})
                   .ok());
    });
    std::array<std::byte, 64> blitOutput{};
    assert(blitReadback.read(0, blitOutput).ok());
    assert(blitOutput[0] == std::byte{0x40});
    assert(blitOutput[1] == std::byte{0x80});
    assert(blitOutput[2] == std::byte{0xbf});
    assert(blitOutput[3] == std::byte{0xff});

    auto multisample = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
        .sampleCount = 4,
    });
    auto resolved = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    assert(multisample.ok() && resolved.ok());
    {
        auto resolveReadback = create_transfer_buffer(
            16, rhi::BufferUsage::copy_destination,
            rhi::MemoryDomain::readback);
        submit_copy([&](rhi::CopyEncoder& copy) {
            const rhi::TextureRegion region{.extent = {2, 2, 1}};
            assert(copy
                       .clear_texture(multisample.value(), region,
                                      {.color = {.r = 0.0F,
                                                 .g = 1.0F,
                                                 .b = 0.0F,
                                                 .a = 1.0F}})
                       .ok());
            assert(copy
                       .resolve_texture(multisample.value(), resolved.value(),
                                        {.source = region,
                                         .destination = region})
                       .ok());
            assert(copy
                       .copy_texture_to_buffer(resolved.value(), resolveReadback,
                                               {.texture = region})
                       .ok());
        });
        std::array<std::byte, 16> resolveOutput{};
        assert(resolveReadback.read(0, resolveOutput).ok());
        assert(resolveOutput[0] == std::byte{0x00});
        assert(resolveOutput[1] == std::byte{0xff});
        assert(resolveOutput[2] == std::byte{0x00});
        assert(resolveOutput[3] == std::byte{0xff});
    }

    const auto initialUploadBudget =
        device.memory_budget(rhi::MemoryDomain::upload);
    const auto initialReadbackBudget =
        device.memory_budget(rhi::MemoryDomain::readback);
    assert(initialUploadBudget.ok() && initialReadbackBudget.ok());
    {
        constexpr std::size_t hostRowPitch = 16;
        constexpr std::size_t hostBytes = hostRowPitch * 2;
        auto hostUpload = device.create_texture({
            .extent = {2, 2, 1},
            .format = rhi::TextureFormat::rgba8_unorm,
            .usage = rhi::TextureUsage::copy_source,
            .memory = rhi::MemoryDomain::upload,
        });
        auto hostReadback = device.create_texture({
            .extent = {2, 2, 1},
            .format = rhi::TextureFormat::rgba8_unorm,
            .usage = rhi::TextureUsage::copy_destination,
            .memory = rhi::MemoryDomain::readback,
        });
        assert(hostUpload.ok() && hostReadback.ok());
        const auto activeUploadBudget =
            device.memory_budget(rhi::MemoryDomain::upload);
        const auto activeReadbackBudget =
            device.memory_budget(rhi::MemoryDomain::readback);
        assert(activeUploadBudget.ok() && activeReadbackBudget.ok());
        assert(activeUploadBudget.value().usedBytes >
               initialUploadBudget.value().usedBytes);
        assert(activeReadbackBudget.value().usedBytes >
               initialReadbackBudget.value().usedBytes);

        std::array<std::byte, hostBytes> hostPixels{};
        for (std::size_t row = 0; row < 2; ++row) {
            for (std::size_t column = 0; column < 8; ++column) {
                const auto index = row * hostRowPitch + column;
                hostPixels[index] =
                    std::byte{static_cast<unsigned char>(0x20u + index)};
            }
        }
        const rhi::TextureRegion hostRegion{.extent = {2, 2, 1}};
        const rhi::TextureDataLayout hostLayout{
            .bytesPerRow = hostRowPitch,
            .rowsPerImage = 2,
        };
        assert(hostUpload.value().write(hostRegion, hostPixels, hostLayout).ok());
        submit_copy([&](rhi::CopyEncoder& copy) {
            assert(copy
                       .copy_texture(hostUpload.value(), hostReadback.value(),
                                     {.source = hostRegion,
                                      .destination = hostRegion})
                       .ok());
        });
        std::array<std::byte, hostBytes> hostOutput{};
        assert(hostReadback.value()
                   .read(hostRegion, hostOutput, hostLayout)
                   .ok());
        assert(hostOutput == hostPixels);
    }
    const auto releasedUploadBudget =
        device.memory_budget(rhi::MemoryDomain::upload);
    const auto releasedReadbackBudget =
        device.memory_budget(rhi::MemoryDomain::readback);
    assert(releasedUploadBudget.ok() && releasedReadbackBudget.ok());
    assert(releasedUploadBudget.value().usedBytes ==
           initialUploadBudget.value().usedBytes);
    assert(releasedReadbackBudget.value().usedBytes ==
           initialReadbackBudget.value().usedBytes);

    const auto deviceBudget =
        device.memory_budget(rhi::MemoryDomain::device_local);
    assert(deviceBudget.ok());
    assert(deviceBudget.value().available_bytes() <
           std::numeric_limits<std::size_t>::max());
    auto overBudgetBuffer = device.create_buffer({
        .size = deviceBudget.value().available_bytes() + 1,
        .usage = rhi::BufferUsage::copy_source,
    });
    assert(!overBudgetBuffer.ok());
    assert(overBudgetBuffer.status().code == rhi::StatusCode::out_of_memory);

    auto externalTexture = device.create_texture({
        .extent = {2, 2, 1},
        .usage = rhi::TextureUsage::copy_source,
        .shareable = true,
    });
    assert(!externalTexture.ok());
    assert(externalTexture.status().code == rhi::StatusCode::unsupported);

    auto lossList = pool.allocate();
    assert(lossList.ok());
    assert(lossList.value().begin().ok());
    assert(lossList.value().end().ok());
    std::array<rhi::CommandList*, 1> lossLists{&lossList.value()};
    rhi::detail::set_vulkan_device_loss_for_testing(true);
    assert(queue.submit(lossLists).code == rhi::StatusCode::device_lost);
    assert(device.lost());
    rhi::detail::set_vulkan_device_loss_for_testing(false);
    assert(queue.submit(lossLists).code == rhi::StatusCode::device_lost);
    auto recoveredDevice = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer,
                             rhi::Feature::compute,
                             rhi::Feature::timestamp_queries},
    });
    assert(recoveredDevice.ok());
    assert(!recoveredDevice.value().lost());
}

} // namespace

int main() {
#if defined(__linux__) && !defined(__ANDROID__)
    verify_vulkan_buffers();
#else
    auto result = truffle::rhi::create_vulkan_instance();
    if (result.ok()) {
        auto instance = std::move(result).value();
        auto adapter = instance.adapter(0);
        assert(adapter.ok());
        assert(adapter.value().info().native);
        assert(adapter.value().info().backend ==
               truffle::rhi::BackendKind::vulkan);
    } else {
        truffle::tests::verify_unavailable_backend(result);
    }
#endif
    return 0;
}

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
    assert(queue.present(swapchain, acquired.imageIndex).ok());
    assert(swapchain.resize({128, 64}).ok());

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
    assert(stats.commandPoolsCreated == 1);
    assert(stats.commandListsCreated == 1);
    assert(stats.drawsRecorded == 1);
    assert(stats.submissions == 1);
    assert(stats.presentations == 1);
    return 0;
}

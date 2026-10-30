#pragma once

// GL fences, semaphores, queries and context ownership. With sync objects
// (macOS GL 4.1) this proves deterministic fence/semaphore timeouts, exact
// occlusion sample counts, explicit unsupported query types and concurrent
// recording/submission from several threads on one context. Elsewhere it
// asserts that semaphores and query pools are reported unsupported.

#include "gl_graphics_tests.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace truffle::tests {

inline void verify_gl_sync(rhi::Result<rhi::Instance> result, bool expectSync,
                           const std::size_t* diagnostics) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    const auto info = adapter.info();
    // No GL context advertises timestamps: macOS reports zero counter bits.
    assert(std::find(info.supportedFeatures.begin(), info.supportedFeatures.end(),
                     rhi::Feature::timestamp_queries) ==
           info.supportedFeatures.end());
    assert(info.timestampPeriodNanoseconds == 0.0);
    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    const auto unsupported_pool = [&](rhi::QueryType type) {
        auto pool = device.create_query_pool({.type = type, .count = 2});
        assert(!pool.ok() && pool.status().code == rhi::StatusCode::unsupported);
    };
    unsupported_pool(rhi::QueryType::timestamp);
    unsupported_pool(rhi::QueryType::pipeline_statistics);
    if (!expectSync) {
        unsupported_pool(rhi::QueryType::occlusion);
        auto semaphore = device.create_semaphore();
        assert(!semaphore.ok() &&
               semaphore.status().code == rhi::StatusCode::unsupported);
        return;
    }

    auto queueResult = device.queue(rhi::QueueKind::graphics);
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    assert(queueResult.ok() && poolResult.ok());
    auto queue = std::move(queueResult).value();
    auto commandPool = std::move(poolResult).value();

    // Fences are timeline values reached when the GL submission returns.
    auto fenceResult = device.create_fence();
    assert(fenceResult.ok());
    auto fence = std::move(fenceResult).value();
    {
        auto list = commandPool.allocate();
        assert(list.ok() && list.value().begin().ok() && list.value().end().ok());
        std::array<rhi::CommandList*, 1> lists{&list.value()};
        assert(queue.submit({.commandLists = lists,
                             .signalFence = &fence,
                             .signalFenceValue = 7})
                   .ok());
        assert(fence.completed_value() == 7);
        assert(fence.wait(7, std::chrono::nanoseconds{1}).ok());
        const auto timeout = fence.wait(8, std::chrono::nanoseconds{1});
        assert(timeout.code == rhi::StatusCode::timeout);
    }

    // A submission waiting on an unreached semaphore times out without
    // consuming its command list, then succeeds once the value is signaled.
    auto blockedResult = device.create_semaphore();
    assert(blockedResult.ok());
    auto blocked = std::move(blockedResult).value();
    {
        auto waiting = commandPool.allocate();
        assert(waiting.ok() && waiting.value().begin().ok() &&
               waiting.value().end().ok());
        std::array<rhi::CommandList*, 1> waitingLists{&waiting.value()};
        const std::array<rhi::SemaphoreWait, 1> blockedWait{{
            {.semaphore = &blocked, .value = 1},
        }};
        const auto timeout = queue.submit({.commandLists = waitingLists,
                                           .waits = blockedWait,
                                           .waitTimeout = std::chrono::nanoseconds{1}});
        assert(timeout.code == rhi::StatusCode::timeout);
        assert(waiting.value().state() == rhi::CommandListState::executable);
        auto release = commandPool.allocate();
        assert(release.ok() && release.value().begin().ok() &&
               release.value().end().ok());
        std::array<rhi::CommandList*, 1> releaseLists{&release.value()};
        const std::array<rhi::SemaphoreSignal, 1> releaseSignal{{
            {.semaphore = &blocked, .value = 1},
        }};
        assert(queue.submit({.commandLists = releaseLists,
                             .signals = releaseSignal})
                   .ok());
        assert(blocked.value() == 1);
        assert(queue.submit({.commandLists = waitingLists,
                             .waits = blockedWait,
                             .waitTimeout = std::chrono::milliseconds{1}})
                   .ok());
    }

    // Occlusion: a fullscreen triangle covers every sample, a 4x4 scissor
    // covers sixteen, and an empty query counts zero.
    constexpr std::uint32_t size = 16;
    auto vertex = gl_fixture_shader(device, true, rhi::ShaderStage::vertex, R"(
const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() { gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0); }
)");
    auto fragment = gl_fixture_shader(device, true, rhi::ShaderStage::fragment, R"(
layout(location=0) out vec4 outputColor;
void main() { outputColor = vec4(1.0); }
)");
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .dynamicState = rhi::DynamicState::viewport | rhi::DynamicState::scissor,
    });
    auto targetResult = device.create_texture({
        .extent = {size, size, 1},
        .usage = rhi::TextureUsage::color_attachment,
    });
    auto queriesResult = device.create_query_pool({
        .type = rhi::QueryType::occlusion, .count = 3});
    auto resultsResult = device.create_buffer({
        .size = 4 * sizeof(std::uint64_t),
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(pipelineResult.ok() && targetResult.ok() && queriesResult.ok() &&
           resultsResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    auto target = std::move(targetResult).value();
    auto queries = std::move(queriesResult).value();
    auto results = std::move(resultsResult).value();
    {
        auto list = commandPool.allocate();
        assert(list.ok() && list.value().begin().ok());
        auto render = list.value().begin_rendering({
            .extent = {size, size},
            .colorAttachments = {{.texture = &target}},
        });
        assert(render.ok());
        auto& encoder = render.value();
        const std::array viewports{rhi::Viewport{0, 0, size, size, 0, 1}};
        const std::array full{rhi::ScissorRect{0, 0, size, size}};
        const std::array corner{rhi::ScissorRect{2, 2, 4, 4}};
        assert(encoder.bind_pipeline(pipeline).ok());
        assert(encoder.set_viewports(0, viewports).ok());
        assert(encoder.set_scissors(0, full).ok());
        assert(encoder.begin_occlusion_query(queries, 0).ok());
        assert(encoder.draw(3).ok());
        assert(encoder.end_occlusion_query().ok());
        assert(encoder.set_scissors(0, corner).ok());
        assert(encoder.begin_occlusion_query(queries, 1).ok());
        assert(encoder.draw(3).ok());
        assert(encoder.end_occlusion_query().ok());
        assert(encoder.begin_occlusion_query(queries, 2).ok());
        assert(encoder.end_occlusion_query().ok());
        assert(encoder.end().ok());
        auto copy = list.value().begin_copy();
        assert(copy.ok());
        assert(copy.value().fill_buffer(results, 0, sizeof(std::uint64_t),
                                        std::byte{0x5a})
                   .ok());
        assert(copy.value().end().ok());
        assert(list.value()
                   .resolve_queries(queries, 0, 3, results, sizeof(std::uint64_t))
                   .ok());
        assert(list.value().end().ok());
        std::array<rhi::CommandList*, 1> lists{&list.value()};
        assert(queue.submit(lists).ok());
        std::array<std::uint64_t, 4> values{};
        assert(results.read(0, std::as_writable_bytes(std::span{values})).ok());
        assert(values[0] == 0x5a5a5a5a5a5a5a5aULL);
        assert(values[1] == size * size);
        assert(values[2] == 16);
        assert(values[3] == 0);
    }

    // Resolving a query that never ended is rejected at submit, and the
    // context keeps working afterwards.
    {
        auto freshResult = device.create_query_pool({
            .type = rhi::QueryType::occlusion, .count = 1});
        assert(freshResult.ok());
        auto list = commandPool.allocate();
        assert(list.ok() && list.value().begin().ok());
        assert(list.value().resolve_queries(freshResult.value(), 0, 1, results).ok());
        assert(list.value().end().ok());
        std::array<rhi::CommandList*, 1> lists{&list.value()};
        assert(queue.submit(lists).code == rhi::StatusCode::invalid_state);
        auto next = commandPool.allocate();
        assert(next.ok() && next.value().begin().ok() && next.value().end().ok());
        std::array<rhi::CommandList*, 1> nextLists{&next.value()};
        assert(queue.submit(nextLists).ok());
    }

    // One context, any thread: each worker records with its own pool and
    // submits to the shared queue; GL calls are serialized by the context.
    constexpr int workers = 4;
    constexpr int iterations = 24;
    constexpr std::size_t bytes = 256;
    std::atomic_int failures{0};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] {
            auto workerPool = device.create_command_pool(rhi::QueueKind::graphics);
            auto workerFence = device.create_fence();
            if (!workerPool.ok() || !workerFence.ok()) {
                ++failures;
                return;
            }
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const auto value = static_cast<std::byte>(worker * 32 + iteration);
                auto buffer = device.create_buffer({
                    .size = bytes,
                    .usage = rhi::BufferUsage::copy_destination,
                    .memory = rhi::MemoryDomain::readback,
                });
                auto list = workerPool.value().allocate();
                if (!buffer.ok() || !list.ok() || !list.value().begin().ok()) {
                    ++failures;
                    return;
                }
                auto copy = list.value().begin_copy();
                if (!copy.ok() ||
                    !copy.value().fill_buffer(buffer.value(), 0, bytes, value).ok() ||
                    !copy.value().end().ok() || !list.value().end().ok()) {
                    ++failures;
                    return;
                }
                std::array<rhi::CommandList*, 1> lists{&list.value()};
                const auto fenceValue = static_cast<std::uint64_t>(iteration + 1);
                if (!queue.submit({.commandLists = lists,
                                   .signalFence = &workerFence.value(),
                                   .signalFenceValue = fenceValue})
                         .ok() ||
                    workerFence.value().completed_value() != fenceValue) {
                    ++failures;
                    return;
                }
                std::array<std::byte, bytes> output{};
                if (!buffer.value().read(0, output).ok() ||
                    std::any_of(output.begin(), output.end(),
                                [&](std::byte byte) { return byte != value; })) {
                    ++failures;
                    return;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    assert(failures.load() == 0);
    if (diagnostics != nullptr) {
        assert(*diagnostics == 0);
    }
}

} // namespace truffle::tests

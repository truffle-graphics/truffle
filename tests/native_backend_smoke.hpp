#pragma once

#include "truffle/rhi/rhi.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

namespace truffle::tests {

inline void verify_native_backend_smoke(rhi::Result<rhi::Instance> result,
                                        rhi::BackendKind backend,
                                        rhi::PlatformKind platform) {
    assert(result.ok());
    auto instance = std::move(result).value();
    assert(instance.backend() == backend);
    assert(instance.adapter_count() == 1);
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().backend == backend);
    assert(adapter.info().platform == platform);
    assert(adapter.info().maturity == rhi::BackendMaturity::native_smoke);
    assert(adapter.info().native);
    assert(!adapter.info().validationOnly);

    auto deviceResult = adapter.request_device();
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    assert(list.end().ok());
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());
}

inline void verify_unavailable_backend(const rhi::Result<rhi::Instance>& result) {
    assert(!result.ok());
    assert(result.status().code == core::StatusCode::unsupported ||
           result.status().code == core::StatusCode::unavailable);
}

inline void verify_native_buffer_backend(rhi::Result<rhi::Instance> result,
                                         rhi::BackendKind backend,
                                         rhi::PlatformKind platform) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    const auto info = adapter.info();
    assert(info.backend == backend && info.platform == platform);
    assert(info.maturity == rhi::BackendMaturity::native_smoke);
    assert(info.native && !info.validationOnly);
    assert(info.resources.bufferViews && info.resources.bufferCopy &&
           info.resources.bufferFill);

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    constexpr std::size_t byteCount = 67;
    auto uploadResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .mappedAtCreation = true,
    });
    auto deviceResultBuffer = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source |
                 rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::device_local,
    });
    auto readbackResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(uploadResult.ok() && deviceResultBuffer.ok() &&
           readbackResult.ok());
    auto upload = std::move(uploadResult).value();
    auto deviceBuffer = std::move(deviceResultBuffer).value();
    auto readback = std::move(readbackResult).value();
    assert(device.create_buffer_view(upload).ok());

    std::array<std::byte, byteCount> expected{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] =
            std::byte{static_cast<unsigned char>((index * 11u) ^ 0x93u)};
    }
    assert(upload.write(0, expected).ok());
    assert(upload.unmap().ok());

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
    assert(encoder.copy_buffer(upload, 0, deviceBuffer, 0, byteCount).ok());
    assert(encoder.copy_buffer(deviceBuffer, 0, readback, 0, byteCount).ok());
    assert(encoder.fill_buffer(readback, 31, 19, std::byte{0x4c}).ok());
    assert(encoder.end().ok());
    assert(list.end().ok());
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());

    std::fill_n(expected.begin() + 31, 19, std::byte{0x4c});
    std::array<std::byte, byteCount> output{};
    assert(readback.read(0, output).ok());
    assert(output == expected);
    auto mapped = readback.map();
    assert(mapped.ok());
    assert(readback.invalidate().ok());
    assert(std::ranges::equal(mapped.value(), expected));
    assert(readback.unmap().ok());
}

} // namespace truffle::tests

#include "truffle/rhi/vulkan_backend.hpp"

#include "native_backend_smoke.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

namespace {

void verify_vulkan_buffers() {
    namespace rhi = truffle::rhi;

    auto instanceResult = rhi::create_vulkan_instance();
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
    assert(!info.resources.textureCopy);

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer,
                             rhi::Feature::memory_budget},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    assert(device.memory_budget(rhi::MemoryDomain::device_local).ok());

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

    auto unsupportedTexture = device.create_texture({
        .extent = {4, 4, 1},
        .usage = rhi::TextureUsage::copy_source,
    });
    assert(!unsupportedTexture.ok());
    assert(unsupportedTexture.status().code ==
           truffle::core::StatusCode::unsupported);
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

#pragma once

#include "truffle/rhi/rhi.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>
#include <vector>

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

inline void verify_native_texture_backend(rhi::Result<rhi::Instance> result,
                                          rhi::BackendKind backend,
                                          rhi::PlatformKind platform) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    const auto info = adapter.info();
    assert(info.backend == backend && info.platform == platform);
    assert(info.resources.bufferTextureCopy && info.resources.textureCopy);
    assert(info.resources.textureClear && info.resources.textureBlitNearest &&
           info.resources.textureBlitLinear);
    assert(!info.resources.textureResolve && !info.resources.externalImport &&
           !info.resources.externalExport);

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    auto sampler = device.create_sampler({
        .minFilter = rhi::Filter::linear,
        .magFilter = rhi::Filter::linear,
        .addressU = rhi::SamplerAddressMode::repeat,
        .addressV = rhi::SamplerAddressMode::mirror_repeat,
    });
    assert(sampler.ok());
    auto anisotropic = device.create_sampler({.maxAnisotropy = 2.0F});
    assert(!anisotropic.ok());
    assert(anisotropic.status().code == rhi::StatusCode::unsupported);

    constexpr std::uint32_t width = 8;
    constexpr std::uint32_t height = 4;
    constexpr std::size_t rowPitch = 64;
    constexpr std::size_t byteCount = rowPitch * height;
    const rhi::TextureDesc deviceTextureDesc{
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination |
                 rhi::TextureUsage::color_attachment,
    };
    auto sourceResult = device.create_texture(deviceTextureDesc);
    auto destinationResult = device.create_texture(deviceTextureDesc);
    auto uploadResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto readbackResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(sourceResult.ok() && destinationResult.ok() && uploadResult.ok() &&
           readbackResult.ok());
    auto source = std::move(sourceResult).value();
    auto destination = std::move(destinationResult).value();
    auto upload = std::move(uploadResult).value();
    auto readback = std::move(readbackResult).value();
    auto unsupportedView = device.create_texture_view(source);
    assert(!unsupportedView.ok());
    assert(unsupportedView.status().code == rhi::StatusCode::unsupported);

    std::array<std::byte, byteCount> expected{};
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width * 4u; ++column) {
            const auto offset = row * rowPitch + column;
            expected[offset] =
                std::byte{static_cast<unsigned char>((offset * 13u) ^ 0x6du)};
        }
    }
    assert(upload.write(0, expected).ok());
    const rhi::BufferTextureCopyRegion region{
        .layout = {.bytesPerRow = rowPitch, .rowsPerImage = height},
        .texture = {
            .subresource = {.aspect = rhi::TextureAspect::color},
            .extent = {width, height, 1},
        },
    };
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(poolResult.ok() && queueResult.ok());
    auto pool = std::move(poolResult).value();
    auto queue = std::move(queueResult).value();

    const auto submit_copy = [&](auto&& record) {
        auto listResult = pool.allocate();
        assert(listResult.ok());
        auto list = std::move(listResult).value();
        assert(list.begin().ok());
        auto copyResult = list.begin_copy();
        assert(copyResult.ok());
        auto copy = std::move(copyResult).value();
        record(copy);
        assert(copy.end().ok());
        assert(list.end().ok());
        std::array<rhi::CommandList*, 1> lists{&list};
        assert(queue.submit(lists).ok());
    };
    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy.fill_buffer(readback, 0, byteCount, std::byte{0}).ok());
        assert(copy.copy_buffer_to_texture(upload, source, region).ok());
        assert(copy.copy_texture(
                       source, destination,
                       {.source = region.texture,
                        .destination = region.texture})
                   .ok());
        assert(copy.copy_texture_to_buffer(destination, readback, region).ok());
    });
    std::array<std::byte, byteCount> output{};
    assert(readback.read(0, output).ok());
    assert(output == expected);

    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy.clear_texture(
                       destination, region.texture,
                       {.color = {0.25F, 0.5F, 0.75F, 1.0F}})
                   .ok());
        assert(copy.fill_buffer(readback, 0, byteCount, std::byte{0}).ok());
        assert(copy.copy_texture_to_buffer(destination, readback, region).ok());
    });
    assert(readback.read(0, output).ok());
    const std::array clearPixel{std::byte{64}, std::byte{128}, std::byte{191},
                                std::byte{255}};
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const auto offset = row * rowPitch + column * clearPixel.size();
            assert(std::equal(clearPixel.begin(), clearPixel.end(),
                              output.begin() + offset));
        }
    }

    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy.blit_texture(
                       source, destination,
                       {.source = region.texture,
                        .destination = region.texture,
                        .filter = rhi::Filter::linear})
                   .ok());
        assert(copy.fill_buffer(readback, 0, byteCount, std::byte{0}).ok());
        assert(copy.copy_texture_to_buffer(destination, readback, region).ok());
    });
    assert(readback.read(0, output).ok());
    assert(output == expected);

    auto hostUpload = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto hostReadback = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(hostUpload.ok() && hostReadback.ok());
    assert(hostUpload.value()
               .write(region.texture, expected,
                      {.bytesPerRow = rowPitch, .rowsPerImage = height})
               .ok());
    submit_copy([&](rhi::CopyEncoder& copy) {
        assert(copy.copy_texture(
                       hostUpload.value(), hostReadback.value(),
                       {.source = region.texture,
                        .destination = region.texture})
                   .ok());
    });
    output.fill(std::byte{0});
    assert(hostReadback.value()
               .read(region.texture, output,
                     {.bytesPerRow = rowPitch, .rowsPerImage = height})
               .ok());
    assert(output == expected);

    auto unsupported3d = device.create_texture({
        .dimension = rhi::TextureDimension::d3,
        .extent = {2, 2, 2},
        .usage = rhi::TextureUsage::copy_destination,
    });
    auto external = device.create_texture({
        .extent = {2, 2, 1},
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::external,
        .shareable = true,
    });
    assert(!unsupported3d.ok() && !external.ok());
    assert(unsupported3d.status().code == rhi::StatusCode::unsupported);
    assert(external.status().code == rhi::StatusCode::unsupported);
}

} // namespace truffle::tests

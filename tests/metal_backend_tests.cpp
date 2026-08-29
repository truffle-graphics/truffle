#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

int main() {
    using namespace truffle;
    auto instanceResult = rhi::create_metal_instance();
    if (!instanceResult.ok()) {
        assert(instanceResult.status().code == core::StatusCode::unavailable);
        return 0;
    }
    auto instance = std::move(instanceResult).value();
    assert(instance.backend() == rhi::BackendKind::metal);
    assert(instance.adapter_count() == 1);

    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().native);
    assert(!adapter.info().validationOnly);
    assert(!adapter.info().presentation);
    assert(adapter.info().resources.bufferCopy);
    assert(adapter.info().resources.bufferTextureCopy);
    assert(adapter.info().resources.textureCopy);
    assert(!adapter.info().resources.textureClear);
    assert(!adapter.info().resources.textureResolve);
    assert(!adapter.info().resources.externalImport);

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer,
                             rhi::Feature::memory_budget},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    assert(!device.lost());
    assert(device.memory_budget(rhi::MemoryDomain::device_local).ok());

    auto unsupportedCompute = device.queue(rhi::QueueKind::compute);
    assert(!unsupportedCompute.ok());
    assert(unsupportedCompute.status().code == core::StatusCode::unsupported);
    auto unsupportedFence = device.create_fence();
    assert(!unsupportedFence.ok());
    assert(unsupportedFence.status().code == core::StatusCode::unsupported);

    constexpr std::size_t byteCount = 64;
    auto uploadResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .mappedAtCreation = true,
        .debugName = "Metal upload proof",
    });
    auto readbackResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
        .debugName = "Metal readback proof",
    });
    assert(uploadResult.ok() && readbackResult.ok());
    auto upload = std::move(uploadResult).value();
    auto readback = std::move(readbackResult).value();
    auto uploadView = device.create_buffer_view(upload);
    assert(uploadView.ok() && uploadView.value().desc().size == byteCount);

    std::array<std::byte, byteCount> sourceBytes{};
    for (std::size_t index = 0; index < sourceBytes.size(); ++index) {
        sourceBytes[index] = std::byte{static_cast<unsigned char>(index ^ 0x5a)};
    }
    assert(upload.write(0, sourceBytes).ok());

    constexpr std::uint32_t width = 4;
    constexpr std::uint32_t height = 4;
    const rhi::TextureRegion wholeTexture{
        .extent = {width, height, 1},
    };
    auto sourceTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .debugName = "Metal texture upload proof",
    });
    auto destinationTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
        .debugName = "Metal texture readback proof",
    });
    assert(sourceTextureResult.ok() && destinationTextureResult.ok());
    auto sourceTexture = std::move(sourceTextureResult).value();
    auto destinationTexture = std::move(destinationTextureResult).value();
    assert(sourceTexture.write(wholeTexture, sourceBytes).ok());
    auto textureView = device.create_texture_view(
        sourceTexture, {.format = rhi::TextureFormat::rgba8_srgb});
    assert(textureView.ok());
    assert(textureView.value().desc().format ==
           rhi::TextureFormat::rgba8_srgb);

    constexpr std::size_t metalRowPitch = 256;
    constexpr std::size_t paddedTextureBytes = metalRowPitch * height;
    auto paddedUploadResult = device.create_buffer({
        .size = paddedTextureBytes,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto paddedReadbackResult = device.create_buffer({
        .size = paddedTextureBytes,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    auto deviceTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    assert(paddedUploadResult.ok() && paddedReadbackResult.ok() &&
           deviceTextureResult.ok());
    auto paddedUpload = std::move(paddedUploadResult).value();
    auto paddedReadback = std::move(paddedReadbackResult).value();
    auto deviceTexture = std::move(deviceTextureResult).value();
    std::array<std::byte, paddedTextureBytes> paddedPixels{};
    for (std::size_t row = 0; row < height; ++row) {
        std::copy_n(sourceBytes.begin() +
                        static_cast<std::ptrdiff_t>(row * width * 4),
                    static_cast<std::ptrdiff_t>(width * 4),
                    paddedPixels.begin() +
                        static_cast<std::ptrdiff_t>(row * metalRowPitch));
    }
    assert(paddedUpload.write(0, paddedPixels).ok());

    auto poolResult = device.create_command_pool(rhi::QueueKind::transfer);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    auto encoderResult = list.begin_copy();
    assert(encoderResult.ok());
    auto encoder = std::move(encoderResult).value();
    assert(encoder
               .copy_buffer(upload, readback,
                            {.sourceOffset = 0,
                             .destinationOffset = 0,
                             .size = byteCount})
               .ok());
    const rhi::BufferTextureCopyRegion paddedRegion{
        .layout = {.bytesPerRow = metalRowPitch, .rowsPerImage = height},
        .texture = wholeTexture,
    };
    assert(encoder
               .copy_buffer_to_texture(paddedUpload, deviceTexture, paddedRegion)
               .ok());
    assert(encoder
               .copy_texture_to_buffer(deviceTexture, paddedReadback,
                                       paddedRegion)
               .ok());
    assert(encoder
               .copy_texture(sourceTexture, destinationTexture,
                             {.source = wholeTexture,
                              .destination = wholeTexture})
               .ok());
    assert(encoder.fill_buffer(paddedReadback, 64, 16, std::byte{0x7f}).ok());
    assert(encoder.end().ok());
    assert(list.end().ok());

    auto queueResult = device.queue(rhi::QueueKind::transfer);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());

    std::array<std::byte, byteCount> bufferOutput{};
    std::array<std::byte, byteCount> textureOutput{};
    assert(readback.read(0, bufferOutput).ok());
    assert(destinationTexture.read(wholeTexture, textureOutput).ok());
    assert(bufferOutput == sourceBytes);
    assert(textureOutput == sourceBytes);
    std::array<std::byte, paddedTextureBytes> paddedOutput{};
    assert(paddedReadback.read(0, paddedOutput).ok());
    std::fill_n(paddedPixels.begin() + 64, 16, std::byte{0x7f});
    assert(paddedOutput == paddedPixels);

    auto unsupportedTexture = device.create_texture({
        .dimension = rhi::TextureDimension::d3,
        .extent = {4, 4, 4},
        .usage = rhi::TextureUsage::copy_source,
    });
    assert(!unsupportedTexture.ok());
    assert(unsupportedTexture.status().code == core::StatusCode::unsupported);

    const auto stats = instance.stats();
    assert(stats.buffersCreated == 4);
    assert(stats.texturesCreated == 3);
    assert(stats.transfersExecuted == 5);
    assert(stats.submissions == 1);
    return 0;
}

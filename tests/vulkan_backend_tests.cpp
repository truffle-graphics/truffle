#include "truffle/rhi/vulkan_backend.hpp"

#include "native_backend_smoke.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
    assert(info.resources.textureViews);
    assert(info.resources.bufferTextureCopy);
    assert(info.resources.textureCopy);
    assert(info.resources.textureClear);
    assert(info.resources.textureResolve);
    assert(info.resources.textureBlitNearest);
    assert(info.resources.textureBlitLinear);

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

    auto externalTexture = device.create_texture({
        .extent = {2, 2, 1},
        .usage = rhi::TextureUsage::copy_source,
        .shareable = true,
    });
    assert(!externalTexture.ok());
    assert(externalTexture.status().code == rhi::StatusCode::unsupported);
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

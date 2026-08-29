#include "rhi_test_utils.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct AllocatorProbe {
    std::size_t reserved = 0;
    std::size_t released = 0;
    std::uint32_t reserveCalls = 0;
    std::uint32_t releaseCalls = 0;
    bool allow = true;
};

bool reserve_memory(truffle::rhi::MemoryDomain, std::size_t size, std::size_t,
                    void* userData) {
    auto& probe = *static_cast<AllocatorProbe*>(userData);
    probe.reserved += size;
    ++probe.reserveCalls;
    return probe.allow;
}

void release_memory(truffle::rhi::MemoryDomain, std::size_t size, std::size_t,
                    void* userData) {
    auto& probe = *static_cast<AllocatorProbe*>(userData);
    probe.released += size;
    ++probe.releaseCalls;
}

struct TransferContext {
    truffle::rhi::CommandPool pool;
    truffle::rhi::CommandList list;
    truffle::rhi::Queue queue;
};

TransferContext make_transfer_context(truffle::rhi::Device& device) {
    auto poolResult =
        device.create_command_pool(truffle::rhi::QueueKind::transfer);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto queueResult = device.queue(truffle::rhi::QueueKind::transfer);
    assert(queueResult.ok());
    return {
        .pool = std::move(pool),
        .list = std::move(listResult).value(),
        .queue = std::move(queueResult).value(),
    };
}

template <typename Record>
void submit_transfer(TransferContext& transfer, Record&& record) {
    assert(transfer.list.reset().ok());
    assert(transfer.list.begin().ok());
    auto encoderResult = transfer.list.begin_copy();
    assert(encoderResult.ok());
    auto encoder = std::move(encoderResult).value();
    record(encoder);
    assert(encoder.end().ok());
    assert(transfer.list.end().ok());
    std::array<truffle::rhi::CommandList*, 1> lists{&transfer.list};
    assert(transfer.queue.submit(lists).ok());
}

truffle::rhi::TextureRegion whole_region(std::uint32_t width,
                                         std::uint32_t height = 1,
                                         std::uint32_t depth = 1,
                                         std::uint32_t layer = 0) {
    return {
        .subresource = {.arrayLayer = layer},
        .extent = {width, height, depth},
    };
}

} // namespace

int main() {
    using namespace truffle;

    AllocatorProbe probe;
    auto instanceResult = rhi::create_null_instance();
    assert(instanceResult.ok());
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().resources.bufferViews);
    assert(adapter.info().resources.textureViews);
    assert(adapter.info().resources.bufferTextureCopy);
    assert(!adapter.info().resources.externalImport);
    assert(!adapter.info().resources.externalExport);
    assert(!adapter.info().resources.textureBlitLinear);

    auto deviceResult = adapter.request_device({
        .allocator = {
            .userData = &probe,
            .reserve = &reserve_memory,
            .release = &release_memory,
        },
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    auto transfer = make_transfer_context(device);

    const auto initialUploadBudget = device.memory_budget(rhi::MemoryDomain::upload);
    assert(initialUploadBudget.ok());
    assert(initialUploadBudget.value().usedBytes == 0);

    {
        auto mappedResult = device.create_buffer({
            .size = 64,
            .usage = rhi::BufferUsage::copy_source |
                     rhi::BufferUsage::copy_destination,
            .memory = rhi::MemoryDomain::upload,
            .mappedAtCreation = true,
        });
        assert(mappedResult.ok());
        auto mappedBuffer = std::move(mappedResult).value();
        assert(mappedBuffer.mapped());
        auto map = mappedBuffer.map();
        assert(map.ok() && map.value().size() == 64);
        map.value()[0] = std::byte{0x2a};
        assert(mappedBuffer.flush(0, 1).ok());
        assert(mappedBuffer.invalidate(0, 1).ok());
        assert(mappedBuffer.unmap().ok());
        assert(!mappedBuffer.mapped());
        assert(!mappedBuffer.unmap().ok());
        assert(mappedBuffer.memory_requirements().size == 64);

        const auto sourceId = mappedBuffer.id();
        auto viewResult = device.create_buffer_view(
            mappedBuffer, {.offset = 16, .size = 32, .stride = 16});
        assert(viewResult.ok());
        auto view = std::move(viewResult).value();
        mappedBuffer = {};
        assert(view.valid());
        assert(view.buffer_id() == sourceId);
        assert(view.desc().size == 32);
        const auto retainedBudget =
            device.memory_budget(rhi::MemoryDomain::upload);
        assert(retainedBudget.ok() && retainedBudget.value().usedBytes >= 64);
        view = {};
    }
    const auto releasedBudget = device.memory_budget(rhi::MemoryDomain::upload);
    assert(releasedBudget.ok() && releasedBudget.value().usedBytes == 0);

    auto oversized = device.create_buffer({
        .size = releasedBudget.value().budgetBytes + 1,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(!oversized.ok());
    assert(oversized.status().code == rhi::StatusCode::out_of_memory);

    auto privateBuffer = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::copy_destination,
    });
    assert(privateBuffer.ok());
    assert(!privateBuffer.value().map().ok());
    assert(privateBuffer.value().flush().code ==
           rhi::StatusCode::unsupported);
    assert(privateBuffer.value().invalidate().code ==
           rhi::StatusCode::unsupported);

    std::array<std::byte, 16> sourceBytes{};
    for (std::size_t index = 0; index < sourceBytes.size(); ++index) {
        sourceBytes[index] = std::byte{static_cast<unsigned char>(index + 1)};
    }
    auto source = tests::make_buffer(device, sourceBytes.size(),
                                     rhi::BufferUsage::copy_source,
                                     rhi::MemoryDomain::upload);
    auto destination = tests::make_buffer(
        device, sourceBytes.size(),
        rhi::BufferUsage::copy_source | rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    assert(source.write(0, sourceBytes).ok());
    std::array<std::byte, 16> zeroes{};
    std::array<std::byte, 16> output{};
    assert(destination.read(0, output).ok() && output == zeroes);

    assert(transfer.list.reset().ok());
    assert(transfer.list.begin().ok());
    auto retainedEncoderResult = transfer.list.begin_copy();
    assert(retainedEncoderResult.ok());
    auto retainedEncoder = std::move(retainedEncoderResult).value();
    assert(retainedEncoder.copy_buffer(source, destination, {}).code ==
           rhi::StatusCode::invalid_argument);
    assert(retainedEncoder
               .copy_buffer(source, destination,
                            {.sourceOffset = 0,
                             .destinationOffset = 0,
                             .size = sourceBytes.size()})
               .ok());
    const auto retainedUsage = device.memory_budget(rhi::MemoryDomain::upload);
    source = {};
    const auto afterOwnerRelease =
        device.memory_budget(rhi::MemoryDomain::upload);
    assert(retainedUsage.ok() && afterOwnerRelease.ok());
    assert(afterOwnerRelease.value().usedBytes == retainedUsage.value().usedBytes);
    assert(retainedEncoder.end().ok());
    assert(transfer.list.end().ok());
    std::array<rhi::CommandList*, 1> retainedLists{&transfer.list};
    assert(transfer.queue.submit(retainedLists).ok());
    assert(destination.read(0, output).ok() && output == sourceBytes);
    const auto afterSubmit = device.memory_budget(rhi::MemoryDomain::upload);
    assert(afterSubmit.ok() &&
           afterSubmit.value().usedBytes < afterOwnerRelease.value().usedBytes);

    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        assert(encoder.fill_buffer(destination, 4, 4, std::byte{0x7f}).ok());
    });
    assert(destination.read(0, output).ok());
    for (std::size_t index = 4; index < 8; ++index) {
        assert(output[index] == std::byte{0x7f});
    }

    auto invalidCube = device.create_texture({
        .dimension = rhi::TextureDimension::cube,
        .extent = {4, 4, 1},
        .arrayLayers = 1,
    });
    assert(!invalidCube.ok());

    auto texture1dResult = device.create_texture({
        .dimension = rhi::TextureDimension::d1,
        .extent = {8, 1, 1},
        .format = rhi::TextureFormat::r8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .mipLevels = 4,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(texture1dResult.ok());
    auto texture1d = std::move(texture1dResult).value();
    const rhi::TextureRegion finalMip{
        .subresource = {.mipLevel = 3},
        .extent = {1, 1, 1},
    };
    std::array<std::byte, 1> finalMipData{std::byte{0x4d}};
    assert(texture1d.write(finalMip, finalMipData).ok());
    std::array<std::byte, 1> finalMipOutput{};
    assert(texture1d.read(finalMip, finalMipOutput).ok());
    assert(finalMipOutput == finalMipData);

    auto textureArrayResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rg8_unorm,
        .usage = rhi::TextureUsage::sampled,
        .arrayLayers = 3,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(textureArrayResult.ok());
    auto textureArray = std::move(textureArrayResult).value();
    auto arrayView = device.create_texture_view(
        textureArray,
        {.format = rhi::TextureFormat::rg8_unorm,
         .range = {.arrayLayerCount = 3}});
    assert(arrayView.ok());

    auto reinterpretTextureResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(reinterpretTextureResult.ok());
    auto reinterpretTexture = std::move(reinterpretTextureResult).value();
    auto reinterpretView = device.create_texture_view(
        reinterpretTexture, {.format = rhi::TextureFormat::rgba8_srgb});
    assert(reinterpretView.ok());
    assert(reinterpretView.value().desc().format ==
           rhi::TextureFormat::rgba8_srgb);

    auto cubeResult = device.create_texture({
        .dimension = rhi::TextureDimension::cube,
        .extent = {4, 4, 1},
        .format = rhi::TextureFormat::rgba8_srgb,
        .usage = rhi::TextureUsage::sampled,
        .arrayLayers = 6,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(cubeResult.ok());
    auto cube = std::move(cubeResult).value();
    auto cubeView = device.create_texture_view(
        cube,
        {.dimension = rhi::TextureDimension::cube,
         .format = rhi::TextureFormat::rgba8_srgb,
         .range = {.arrayLayerCount = 6}});
    assert(cubeView.ok());

    auto texture3dResult = device.create_texture({
        .dimension = rhi::TextureDimension::d3,
        .extent = {2, 2, 2},
        .format = rhi::TextureFormat::r8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(texture3dResult.ok());
    auto texture3d = std::move(texture3dResult).value();
    std::array<std::byte, 8> volumeData{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    assert(texture3d.write(whole_region(2, 2, 2), volumeData).ok());
    std::array<std::byte, 8> volumeReadback{};
    assert(texture3d.read(whole_region(2, 2, 2), volumeReadback).ok());
    assert(volumeReadback == volumeData);

    auto compressedResult = device.create_texture({
        .extent = {7, 7, 1},
        .format = rhi::TextureFormat::bc1_rgba_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(compressedResult.ok());
    auto compressed = std::move(compressedResult).value();
    std::array<std::byte, 32> compressedData{};
    compressedData[0] = std::byte{0x55};
    assert(compressed.write(whole_region(7, 7), compressedData).ok());
    std::array<std::byte, 32> compressedOutput{};
    assert(compressed.read(whole_region(7, 7), compressedOutput).ok());
    assert(compressedOutput == compressedData);

    constexpr std::uint32_t textureWidth = 4;
    constexpr std::uint32_t textureHeight = 4;
    std::array<std::byte, textureWidth * textureHeight * 4> pixels{};
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = std::byte{static_cast<unsigned char>(index)};
    }
    const rhi::TextureDesc uploadTextureDesc{
        .extent = {textureWidth, textureHeight, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    };
    auto sourceTextureResult = device.create_texture(uploadTextureDesc);
    assert(sourceTextureResult.ok());
    auto sourceTexture = std::move(sourceTextureResult).value();
    assert(sourceTexture.write(whole_region(textureWidth, textureHeight), pixels)
               .ok());
    auto destinationTextureResult = device.create_texture({
        .extent = {textureWidth, textureHeight, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(destinationTextureResult.ok());
    auto destinationTexture = std::move(destinationTextureResult).value();
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .copy_texture(sourceTexture, destinationTexture,
                                 {.source = whole_region(textureWidth,
                                                         textureHeight),
                                  .destination = whole_region(textureWidth,
                                                              textureHeight)})
                   .ok());
    });
    std::array<std::byte, pixels.size()> textureOutput{};
    assert(destinationTexture
               .read(whole_region(textureWidth, textureHeight), textureOutput)
               .ok());
    assert(textureOutput == pixels);

    auto uploadBuffer = tests::make_buffer(
        device, pixels.size(), rhi::BufferUsage::copy_source,
        rhi::MemoryDomain::upload);
    auto readbackBuffer = tests::make_buffer(
        device, pixels.size(), rhi::BufferUsage::copy_destination,
        rhi::MemoryDomain::readback);
    auto deviceTextureResult = device.create_texture({
        .extent = {textureWidth, textureHeight, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    assert(deviceTextureResult.ok());
    auto deviceTexture = std::move(deviceTextureResult).value();
    assert(uploadBuffer.write(0, pixels).ok());
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        const rhi::BufferTextureCopyRegion region{
            .texture = whole_region(textureWidth, textureHeight),
        };
        assert(encoder.copy_buffer_to_texture(uploadBuffer, deviceTexture, region)
                   .ok());
        assert(encoder.copy_texture_to_buffer(deviceTexture, readbackBuffer, region)
                   .ok());
    });
    std::array<std::byte, pixels.size()> bufferTextureOutput{};
    assert(readbackBuffer.read(0, bufferTextureOutput).ok());
    assert(bufferTextureOutput == pixels);

    auto clearTextureResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(clearTextureResult.ok());
    auto clearTexture = std::move(clearTextureResult).value();
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .clear_texture(clearTexture, whole_region(2, 2),
                                  {.color = {.r = 1.0F,
                                             .g = 0.5F,
                                             .b = 0.0F,
                                             .a = 1.0F}})
                   .ok());
    });
    std::array<std::byte, 16> clearOutput{};
    assert(clearTexture.read(whole_region(2, 2), clearOutput).ok());
    assert(clearOutput[0] == std::byte{0xff});
    assert(clearOutput[1] == std::byte{0x80});
    assert(clearOutput[2] == std::byte{0x00});
    assert(clearOutput[3] == std::byte{0xff});

    auto blitSourceResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto blitDestinationResult = device.create_texture({
        .extent = {4, 4, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(blitSourceResult.ok() && blitDestinationResult.ok());
    auto blitSource = std::move(blitSourceResult).value();
    auto blitDestination = std::move(blitDestinationResult).value();
    std::array<std::byte, 16> blitPixels{};
    blitPixels[0] = std::byte{0xaa};
    assert(blitSource.write(whole_region(2, 2), blitPixels).ok());
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .blit_texture(blitSource, blitDestination,
                                 {.source = whole_region(2, 2),
                                  .destination = whole_region(4, 4),
                                  .filter = rhi::Filter::nearest})
                   .ok());
    });
    std::array<std::byte, 64> blitOutput{};
    assert(blitDestination.read(whole_region(4, 4), blitOutput).ok());
    assert(blitOutput[0] == std::byte{0xaa});
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        const auto linear = encoder.blit_texture(
            blitSource, blitDestination,
            {.source = whole_region(2, 2),
             .destination = whole_region(4, 4),
             .filter = rhi::Filter::linear});
        assert(linear.code == rhi::StatusCode::unsupported);
    });

    auto multisampleResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .sampleCount = 4,
        .memory = rhi::MemoryDomain::upload,
    });
    auto resolvedResult = device.create_texture({
        .extent = {2, 2, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(multisampleResult.ok() && resolvedResult.ok());
    auto multisample = std::move(multisampleResult).value();
    auto resolved = std::move(resolvedResult).value();
    assert(multisample.write(whole_region(2, 2), blitPixels).ok());
    submit_transfer(transfer, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .resolve_texture(multisample, resolved,
                                    {.source = whole_region(2, 2),
                                     .destination = whole_region(2, 2)})
                   .ok());
    });
    std::array<std::byte, 16> resolveOutput{};
    assert(resolved.read(whole_region(2, 2), resolveOutput).ok());
    assert(resolveOutput == blitPixels);

    auto externalBuffer = device.import_buffer(
        {.size = 16,
         .usage = rhi::BufferUsage::copy_source,
         .memory = rhi::MemoryDomain::external},
        {.type = rhi::ExternalHandleType::opaque_pointer, .value = 1});
    assert(!externalBuffer.ok());
    assert(externalBuffer.status().code == rhi::StatusCode::unsupported);
    assert(!destination.export_memory().ok());

    probe.allow = false;
    auto denied = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(!denied.ok());
    assert(denied.status().code == rhi::StatusCode::out_of_memory);
    probe.allow = true;

    assert(instance.stats().transfersExecuted >= 8);
    assert(probe.reserveCalls != 0);
    assert(probe.reserved >= probe.released);
    return 0;
}

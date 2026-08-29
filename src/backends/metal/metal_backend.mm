#import <Metal/Metal.h>

#include "truffle/rhi/metal_backend.hpp"

#include "foundation_backend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace truffle::rhi {
namespace {

struct MetalBufferResource {
    explicit MetalBufferResource(id<MTLBuffer> bufferValue)
        : buffer(bufferValue) {}
    ~MetalBufferResource() { [buffer release]; }
    MetalBufferResource(const MetalBufferResource&) = delete;
    MetalBufferResource& operator=(const MetalBufferResource&) = delete;

    id<MTLBuffer> buffer = nil;
};

struct MetalTextureResource {
    MetalTextureResource(id<MTLTexture> textureValue, TextureDesc descValue,
                         std::uint32_t bytesPerPixelValue)
        : texture(textureValue), desc(std::move(descValue)),
          bytesPerPixel(bytesPerPixelValue) {}
    ~MetalTextureResource() { [texture release]; }
    MetalTextureResource(const MetalTextureResource&) = delete;
    MetalTextureResource& operator=(const MetalTextureResource&) = delete;

    id<MTLTexture> texture = nil;
    TextureDesc desc;
    std::uint32_t bytesPerPixel = 0;
};

struct MetalTextureViewResource {
    explicit MetalTextureViewResource(id<MTLTexture> textureValue)
        : texture(textureValue) {}
    ~MetalTextureViewResource() { [texture release]; }
    MetalTextureViewResource(const MetalTextureViewResource&) = delete;
    MetalTextureViewResource& operator=(const MetalTextureViewResource&) = delete;

    id<MTLTexture> texture = nil;
};

[[nodiscard]] id<MTLDevice> system_device() {
    static std::once_flag once;
    static id<MTLDevice> device = nil;
    std::call_once(once, [] { device = MTLCreateSystemDefaultDevice(); });
    return device;
}

[[nodiscard]] Status metal_failure(StatusCode code, std::string message,
                                   NSInteger nativeCode = 0) {
    return Status::failure(
        code, message,
        BackendDiagnostic{
            .domain = "Metal",
            .nativeCode = nativeCode,
            .message = std::move(message),
        });
}

[[nodiscard]] MTLResourceOptions buffer_options(MemoryDomain domain) {
    switch (domain) {
    case MemoryDomain::upload:
    case MemoryDomain::readback:
        return MTLResourceStorageModeShared;
    case MemoryDomain::device_local:
        return MTLResourceStorageModePrivate;
    case MemoryDomain::external:
        break;
    }
    return MTLResourceStorageModePrivate;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_buffer(
    const BufferDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        if (desc.memory == MemoryDomain::external || desc.shareable) {
            return Status::failure(StatusCode::unsupported,
                                   "Metal external buffer memory is not implemented");
        }
        id<MTLBuffer> buffer =
            [device newBufferWithLength:desc.size
                                options:buffer_options(desc.memory)];
        if (buffer == nil) {
            return metal_failure(StatusCode::out_of_memory,
                                 "Metal buffer allocation failed");
        }
        if (!desc.debugName.empty()) {
            buffer.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalBufferResource>(buffer));
    }
}

[[nodiscard]] Result<std::span<std::byte>> map_metal_buffer(
    const std::shared_ptr<void>& resource) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil ||
        metal->buffer.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal buffers are not host mappable");
    }
    auto* bytes = static_cast<std::byte*>(metal->buffer.contents);
    if (bytes == nullptr) {
        return metal_failure(StatusCode::backend_error,
                             "Metal buffer returned no mapped contents");
    }
    return std::span<std::byte>{bytes, metal->buffer.length};
}

[[nodiscard]] Status unmap_metal_buffer(const std::shared_ptr<void>& resource) {
    return resource ? Status::success()
                    : Status::failure(StatusCode::invalid_argument,
                                      "Metal buffer resource is invalid");
}

[[nodiscard]] Status flush_metal_buffer(const std::shared_ptr<void>& resource,
                                        std::size_t offset,
                                        std::size_t size) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil || offset > metal->buffer.length ||
        size > metal->buffer.length - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer flush range is invalid");
    }
#if TARGET_OS_OSX
    if (metal->buffer.storageMode == MTLStorageModeManaged) {
        [metal->buffer didModifyRange:NSMakeRange(offset, size)];
    }
#endif
    return Status::success();
}

[[nodiscard]] Status invalidate_metal_buffer(
    const std::shared_ptr<void>& resource, std::size_t offset,
    std::size_t size) {
    const auto metal = std::static_pointer_cast<MetalBufferResource>(resource);
    if (!metal || metal->buffer == nil || offset > metal->buffer.length ||
        size > metal->buffer.length - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer invalidate range is invalid");
    }
    return Status::success();
}

[[nodiscard]] Status write_metal_buffer(
    const std::shared_ptr<void>& resource, std::size_t offset,
    std::span<const std::byte> data) {
    auto mapped = map_metal_buffer(resource);
    if (!mapped.ok()) {
        return mapped.status();
    }
    auto bytes = std::move(mapped).value();
    if (offset > bytes.size() || data.size() > bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer write exceeds allocation");
    }
    std::copy(data.begin(), data.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return flush_metal_buffer(resource, offset, data.size());
}

[[nodiscard]] Status read_metal_buffer(const std::shared_ptr<void>& resource,
                                       std::size_t offset,
                                       std::span<std::byte> data) {
    if (auto status = invalidate_metal_buffer(resource, offset, data.size());
        !status.ok()) {
        return status;
    }
    auto mapped = map_metal_buffer(resource);
    if (!mapped.ok()) {
        return mapped.status();
    }
    const auto bytes = std::move(mapped).value();
    if (offset > bytes.size() || data.size() > bytes.size() - offset) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal buffer read exceeds allocation");
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                static_cast<std::ptrdiff_t>(data.size()), data.begin());
    return Status::success();
}

struct MetalFormat {
    MTLPixelFormat format = MTLPixelFormatInvalid;
    std::uint32_t bytesPerPixel = 0;
};

[[nodiscard]] MetalFormat metal_format(TextureFormat format) {
    switch (format) {
    case TextureFormat::r8_unorm:
        return {MTLPixelFormatR8Unorm, 1};
    case TextureFormat::rg8_unorm:
        return {MTLPixelFormatRG8Unorm, 2};
    case TextureFormat::rgba8_unorm:
        return {MTLPixelFormatRGBA8Unorm, 4};
    case TextureFormat::rgba8_srgb:
        return {MTLPixelFormatRGBA8Unorm_sRGB, 4};
    case TextureFormat::bgra8_unorm:
        return {MTLPixelFormatBGRA8Unorm, 4};
    case TextureFormat::bgra8_srgb:
        return {MTLPixelFormatBGRA8Unorm_sRGB, 4};
    case TextureFormat::rgba16_float:
        return {MTLPixelFormatRGBA16Float, 8};
    case TextureFormat::rgba32_float:
        return {MTLPixelFormatRGBA32Float, 16};
    case TextureFormat::depth16_unorm:
        return {MTLPixelFormatDepth16Unorm, 2};
    case TextureFormat::depth32_float:
        return {MTLPixelFormatDepth32Float, 4};
    default:
        return {};
    }
}

[[nodiscard]] MTLTextureUsage metal_texture_usage(TextureUsage usage) {
    MTLTextureUsage result = MTLTextureUsagePixelFormatView;
    if (has_usage(usage, TextureUsage::sampled)) {
        result |= MTLTextureUsageShaderRead;
    }
    if (has_usage(usage, TextureUsage::storage)) {
        result |= MTLTextureUsageShaderWrite;
    }
    if (has_usage(usage, TextureUsage::color_attachment) ||
        has_usage(usage, TextureUsage::depth_stencil_attachment)) {
        result |= MTLTextureUsageRenderTarget;
    }
    return result;
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_texture(
    const TextureDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        const auto format = metal_format(desc.format);
        if (desc.dimension != TextureDimension::d2 || desc.extent.depth != 1 ||
            desc.arrayLayers != 1 || desc.sampleCount != 1 ||
            format.format == MTLPixelFormatInvalid || desc.shareable ||
            desc.memory == MemoryDomain::external) {
            return Status::failure(
                StatusCode::unsupported,
                "this Metal texture shape, format, sample count, or memory mode is unsupported");
        }
        auto* descriptor = [[MTLTextureDescriptor alloc] init];
        descriptor.textureType = MTLTextureType2D;
        descriptor.pixelFormat = format.format;
        descriptor.width = desc.extent.width;
        descriptor.height = desc.extent.height;
        descriptor.depth = 1;
        descriptor.mipmapLevelCount = desc.mipLevels;
        descriptor.arrayLength = 1;
        descriptor.sampleCount = 1;
        descriptor.storageMode = desc.memory == MemoryDomain::device_local
                                     ? MTLStorageModePrivate
                                     : MTLStorageModeShared;
        descriptor.usage = metal_texture_usage(desc.usage);
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        [descriptor release];
        if (texture == nil) {
            return metal_failure(StatusCode::out_of_memory,
                                 "Metal texture allocation failed");
        }
        if (!desc.debugName.empty()) {
            texture.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalTextureResource>(texture, desc,
                                                   format.bytesPerPixel));
    }
}

[[nodiscard]] Result<std::shared_ptr<void>> create_metal_texture_view(
    const std::shared_ptr<void>& resource, const TextureViewDesc& desc) {
    @autoreleasepool {
        const auto metal =
            std::static_pointer_cast<MetalTextureResource>(resource);
        const auto format = metal_format(desc.format);
        if (!metal || metal->texture == nil ||
            format.format == MTLPixelFormatInvalid ||
            desc.dimension != TextureDimension::d2) {
            return Status::failure(StatusCode::unsupported,
                                   "this Metal texture view is unsupported");
        }
        id<MTLTexture> view = [metal->texture
            newTextureViewWithPixelFormat:format.format
                               textureType:MTLTextureType2D
                                    levels:NSMakeRange(desc.range.baseMipLevel,
                                                       desc.range.mipLevelCount)
                                    slices:NSMakeRange(desc.range.baseArrayLayer,
                                                       desc.range.arrayLayerCount)];
        if (view == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal texture view creation failed");
        }
        return std::static_pointer_cast<void>(
            std::make_shared<MetalTextureViewResource>(view));
    }
}

[[nodiscard]] bool metal_texture_region_valid(
    const MetalTextureResource& texture, const TextureRegion& region) {
    if (region.subresource.mipLevel >= texture.desc.mipLevels ||
        region.subresource.arrayLayer != 0 || region.origin.z != 0 ||
        region.extent.depth != 1 || region.extent.width == 0 ||
        region.extent.height == 0) {
        return false;
    }
    const auto width =
        std::max(1u, texture.desc.extent.width >> region.subresource.mipLevel);
    const auto height =
        std::max(1u, texture.desc.extent.height >> region.subresource.mipLevel);
    return region.origin.x <= width &&
           region.extent.width <= width - region.origin.x &&
           region.origin.y <= height &&
           region.extent.height <= height - region.origin.y;
}

[[nodiscard]] std::size_t required_texture_bytes(
    const MetalTextureResource& texture, const TextureRegion& region,
    const TextureDataLayout& layout) {
    if (!metal_texture_region_valid(texture, region)) {
        return 0;
    }
    const auto tightRow = static_cast<std::size_t>(region.extent.width) *
                          texture.bytesPerPixel;
    const auto rowBytes = layout.bytesPerRow == 0 ? tightRow : layout.bytesPerRow;
    if (rowBytes < tightRow) {
        return 0;
    }
    return layout.offset +
           (static_cast<std::size_t>(region.extent.height) - 1u) * rowBytes +
           tightRow;
}

[[nodiscard]] Status write_metal_texture(
    const std::shared_ptr<void>& resource, const TextureRegion& region,
    std::span<const std::byte> data, const TextureDataLayout& layout) {
    const auto metal = std::static_pointer_cast<MetalTextureResource>(resource);
    if (!metal || metal->texture == nil ||
        metal->texture.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal textures require transfer upload");
    }
    const auto required = required_texture_bytes(*metal, region, layout);
    if (required == 0 || required > data.size()) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal texture write layout is invalid");
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    metal->bytesPerPixel
                              : layout.bytesPerRow;
    const auto nativeRegion = MTLRegionMake2D(
        region.origin.x, region.origin.y, region.extent.width,
        region.extent.height);
    [metal->texture replaceRegion:nativeRegion
                      mipmapLevel:region.subresource.mipLevel
                        withBytes:data.data() + layout.offset
                      bytesPerRow:rowBytes];
    return Status::success();
}

[[nodiscard]] Status read_metal_texture(
    const std::shared_ptr<void>& resource, const TextureRegion& region,
    std::span<std::byte> data, const TextureDataLayout& layout) {
    const auto metal = std::static_pointer_cast<MetalTextureResource>(resource);
    if (!metal || metal->texture == nil ||
        metal->texture.storageMode == MTLStorageModePrivate) {
        return Status::failure(StatusCode::unsupported,
                               "private Metal textures require transfer readback");
    }
    const auto required = required_texture_bytes(*metal, region, layout);
    if (required == 0 || required > data.size()) {
        return Status::failure(StatusCode::invalid_argument,
                               "Metal texture read layout is invalid");
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    metal->bytesPerPixel
                              : layout.bytesPerRow;
    const auto nativeRegion = MTLRegionMake2D(
        region.origin.x, region.origin.y, region.extent.width,
        region.extent.height);
    [metal->texture getBytes:data.data() + layout.offset
                 bytesPerRow:rowBytes
                  fromRegion:nativeRegion
                 mipmapLevel:region.subresource.mipLevel];
    return Status::success();
}

[[nodiscard]] MTLOrigin metal_origin(Origin3D origin) {
    return MTLOriginMake(origin.x, origin.y, origin.z);
}

[[nodiscard]] MTLSize metal_size(Extent3D extent) {
    return MTLSizeMake(extent.width, extent.height, extent.depth);
}

[[nodiscard]] Status encode_metal_transfer(id<MTLBlitCommandEncoder> encoder,
                                           const detail::NativeTransfer& transfer) {
    switch (transfer.kind) {
    case detail::NativeTransferKind::copy_buffer: {
        const auto source =
            std::static_pointer_cast<MetalBufferResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        [encoder copyFromBuffer:source->buffer
                   sourceOffset:transfer.buffer.sourceOffset
                       toBuffer:destination->buffer
              destinationOffset:transfer.buffer.destinationOffset
                           size:transfer.buffer.size];
        return Status::success();
    }
    case detail::NativeTransferKind::fill_buffer: {
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!destination) {
            break;
        }
        [encoder fillBuffer:destination->buffer
                      range:NSMakeRange(transfer.buffer.destinationOffset,
                                        transfer.buffer.size)
                      value:std::to_integer<std::uint8_t>(transfer.fillValue)];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_buffer_to_texture: {
        const auto source =
            std::static_pointer_cast<MetalBufferResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalTextureResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        const auto rowBytes = transfer.bufferTexture.layout.bytesPerRow == 0
                                  ? transfer.bufferTexture.texture.extent.width *
                                        destination->bytesPerPixel
                                  : transfer.bufferTexture.layout.bytesPerRow;
        const auto rows = transfer.bufferTexture.layout.rowsPerImage == 0
                              ? transfer.bufferTexture.texture.extent.height
                              : transfer.bufferTexture.layout.rowsPerImage;
        [encoder copyFromBuffer:source->buffer
                   sourceOffset:transfer.bufferTexture.bufferOffset +
                                transfer.bufferTexture.layout.offset
              sourceBytesPerRow:rowBytes
            sourceBytesPerImage:rowBytes * rows
                     sourceSize:metal_size(transfer.bufferTexture.texture.extent)
                      toTexture:destination->texture
               destinationSlice:transfer.bufferTexture.texture.subresource.arrayLayer
               destinationLevel:transfer.bufferTexture.texture.subresource.mipLevel
              destinationOrigin:metal_origin(transfer.bufferTexture.texture.origin)];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_texture_to_buffer: {
        const auto source =
            std::static_pointer_cast<MetalTextureResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalBufferResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        const auto rowBytes = transfer.bufferTexture.layout.bytesPerRow == 0
                                  ? transfer.bufferTexture.texture.extent.width *
                                        source->bytesPerPixel
                                  : transfer.bufferTexture.layout.bytesPerRow;
        const auto rows = transfer.bufferTexture.layout.rowsPerImage == 0
                              ? transfer.bufferTexture.texture.extent.height
                              : transfer.bufferTexture.layout.rowsPerImage;
        [encoder copyFromTexture:source->texture
                    sourceSlice:transfer.bufferTexture.texture.subresource.arrayLayer
                    sourceLevel:transfer.bufferTexture.texture.subresource.mipLevel
                   sourceOrigin:metal_origin(transfer.bufferTexture.texture.origin)
                     sourceSize:metal_size(transfer.bufferTexture.texture.extent)
                       toBuffer:destination->buffer
              destinationOffset:transfer.bufferTexture.bufferOffset +
                                transfer.bufferTexture.layout.offset
         destinationBytesPerRow:rowBytes
       destinationBytesPerImage:rowBytes * rows];
        return Status::success();
    }
    case detail::NativeTransferKind::copy_texture: {
        const auto source =
            std::static_pointer_cast<MetalTextureResource>(transfer.source);
        const auto destination =
            std::static_pointer_cast<MetalTextureResource>(transfer.destination);
        if (!source || !destination) {
            break;
        }
        [encoder copyFromTexture:source->texture
                    sourceSlice:transfer.texture.source.subresource.arrayLayer
                    sourceLevel:transfer.texture.source.subresource.mipLevel
                   sourceOrigin:metal_origin(transfer.texture.source.origin)
                     sourceSize:metal_size(transfer.texture.source.extent)
                      toTexture:destination->texture
               destinationSlice:transfer.texture.destination.subresource.arrayLayer
               destinationLevel:transfer.texture.destination.subresource.mipLevel
              destinationOrigin:metal_origin(transfer.texture.destination.origin)];
        return Status::success();
    }
    case detail::NativeTransferKind::clear_texture:
    case detail::NativeTransferKind::resolve_texture:
    case detail::NativeTransferKind::blit_texture:
        return Status::failure(StatusCode::unsupported,
                               "this Metal texture transfer is unsupported");
    }
    return Status::failure(StatusCode::invalid_argument,
                           "Metal transfer resources are invalid");
}

[[nodiscard]] Status submit_metal_commands(
    std::span<const detail::NativeTransfer> transfers) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "Metal device is no longer available");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil) {
            return metal_failure(StatusCode::backend_error,
                                 "Metal command queue creation failed");
        }
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (command == nil) {
            [queue release];
            return metal_failure(StatusCode::backend_error,
                                 "Metal command buffer creation failed");
        }
        if (!transfers.empty()) {
            id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
            if (encoder == nil) {
                [queue release];
                return metal_failure(StatusCode::backend_error,
                                     "Metal blit encoder creation failed");
            }
            for (const auto& transfer : transfers) {
                if (auto status = encode_metal_transfer(encoder, transfer);
                    !status.ok()) {
                    [encoder endEncoding];
                    [queue release];
                    return status;
                }
            }
            [encoder endEncoding];
        }
        [command commit];
        [command waitUntilCompleted];
        const auto commandStatus = command.status;
        const auto error = command.error;
        [queue release];
        if (commandStatus == MTLCommandBufferStatusError) {
            const auto message = error.localizedDescription != nil
                                     ? std::string{error.localizedDescription.UTF8String}
                                     : std::string{"Metal command submission failed"};
            return metal_failure(StatusCode::backend_error, message,
                                 error.code);
        }
    }
    return Status::success();
}

} // namespace

Result<Instance> create_metal_instance(const InstanceDesc& desc) {
    @autoreleasepool {
        const auto device = system_device();
        if (device == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "no native Metal device is available");
        }
        const auto* name = device.name.UTF8String;
        const auto deviceBudget =
            static_cast<std::size_t>(device.recommendedMaxWorkingSetSize);
        return detail::create_foundation_instance(
            desc,
            {
                .kind = BackendKind::metal,
                .adapterName = name != nullptr ? name : "Metal adapter",
                .queueKinds = {QueueKind::graphics, QueueKind::transfer},
                .supportedFeatures = {Feature::transfer,
                                      Feature::memory_budget},
                .resourceCapabilities = {
                    .bufferViews = true,
                    .textureViews = true,
                    .hostCoherent = true,
                    .bufferCopy = true,
                    .bufferFill = true,
                    .bufferTextureCopy = true,
                    .textureCopy = true,
                    .textureClear = false,
                    .textureResolve = false,
                    .textureBlitNearest = false,
                    .textureBlitLinear = false,
                    .externalImport = false,
                    .externalExport = false,
                },
                .uploadBudgetBytes = 512u * 1024u * 1024u,
                .readbackBudgetBytes = 512u * 1024u * 1024u,
                .deviceLocalBudgetBytes =
                    deviceBudget != 0 ? deviceBudget : 1024u * 1024u * 1024u,
                .native = true,
                .validationOnly = false,
                .presentation = false,
                .logicalResources = false,
                .createBuffer = &create_metal_buffer,
                .mapBuffer = &map_metal_buffer,
                .unmapBuffer = &unmap_metal_buffer,
                .flushBuffer = &flush_metal_buffer,
                .invalidateBuffer = &invalidate_metal_buffer,
                .writeBuffer = &write_metal_buffer,
                .readBuffer = &read_metal_buffer,
                .createTexture = &create_metal_texture,
                .createTextureView = &create_metal_texture_view,
                .writeTexture = &write_metal_texture,
                .readTexture = &read_metal_texture,
                .nativeSubmit = &submit_metal_commands,
            });
    }
}

} // namespace truffle::rhi

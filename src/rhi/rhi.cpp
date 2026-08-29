#include "truffle/rhi/rhi.hpp"

#include "foundation_backend.hpp"
#include "truffle/rhi/validation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace truffle::rhi {

const ResourceBinding* PipelineReflection::find(std::uint32_t group,
                                                std::uint32_t binding,
                                                ShaderStage stage) const noexcept {
    for (const auto& candidate : bindings_) {
        if (candidate.group == group && candidate.binding == binding &&
            candidate.stage == stage) {
            return &candidate;
        }
    }
    return nullptr;
}

namespace detail {

using Handle = std::uint64_t;

enum class ObjectKind {
    instance,
    adapter,
    device,
    queue,
    command_pool,
    command_list,
    buffer,
    buffer_view,
    texture,
    texture_view,
    shader,
    pipeline,
    compute_pipeline,
    fence,
    semaphore,
    query_pool,
    surface,
    swapchain,
    upload_ring,
};

struct Runtime;

struct BackendDispatch {
    Result<Handle> (*create_adapter)(Runtime&, std::size_t);
    Result<Handle> (*create_device)(Runtime&, Handle, const DeviceDesc&);
    Result<Handle> (*create_queue)(Runtime&, Handle, QueueKind);
    Result<Handle> (*create_command_pool)(Runtime&, Handle, QueueKind);
    Result<Handle> (*allocate_command_list)(Runtime&, Handle);
    Result<Handle> (*create_buffer)(Runtime&, Handle, const BufferDesc&);
    Result<Handle> (*create_buffer_view)(Runtime&, Handle, Handle,
                                         const BufferViewDesc&);
    Result<Handle> (*create_texture)(Runtime&, Handle, const TextureDesc&);
    Result<Handle> (*create_texture_view)(Runtime&, Handle, Handle,
                                          const TextureViewDesc&);
    Result<Handle> (*import_buffer)(Runtime&, Handle, const BufferDesc&,
                                    ExternalMemoryHandle);
    Result<Handle> (*import_texture)(Runtime&, Handle, const TextureDesc&,
                                     ExternalMemoryHandle);
    Result<Handle> (*create_shader)(Runtime&, Handle, const ShaderDesc&);
    Result<Handle> (*create_pipeline)(Runtime&, Handle, const PipelineDesc&);
    Result<Handle> (*create_compute_pipeline)(Runtime&, Handle,
                                               const ComputePipelineDesc&);
    Result<Handle> (*create_fence)(Runtime&, Handle, const FenceDesc&);
    Result<Handle> (*create_semaphore)(Runtime&, Handle, const SemaphoreDesc&);
    Result<Handle> (*create_query_pool)(Runtime&, Handle, const QueryPoolDesc&);
    Result<Handle> (*create_surface)(Runtime&, Handle, const SurfaceDesc&);
    Result<Handle> (*create_swapchain)(Runtime&, Handle, Handle,
                                       const SwapchainDesc&);
    Result<Handle> (*create_upload_ring)(Runtime&, Handle, std::uint32_t,
                                         std::size_t);
    Status (*submit)(Runtime&, Handle, std::span<const Handle>, Handle,
                     std::uint64_t);
    Status (*present)(Runtime&, Handle, Handle, std::uint32_t);
};

struct FormatInfo {
    std::uint32_t blockWidth = 1;
    std::uint32_t blockHeight = 1;
    std::uint32_t bytesPerBlock = 0;
    TextureAspect aspects = TextureAspect::none;
    bool compressed = false;
};

[[nodiscard]] constexpr FormatInfo format_info(TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::r8_unorm:
        return {1, 1, 1, TextureAspect::color, false};
    case TextureFormat::rg8_unorm:
        return {1, 1, 2, TextureAspect::color, false};
    case TextureFormat::rgba8_unorm:
    case TextureFormat::rgba8_srgb:
    case TextureFormat::bgra8_unorm:
    case TextureFormat::bgra8_srgb:
        return {1, 1, 4, TextureAspect::color, false};
    case TextureFormat::rgba16_float:
        return {1, 1, 8, TextureAspect::color, false};
    case TextureFormat::rgba32_float:
        return {1, 1, 16, TextureAspect::color, false};
    case TextureFormat::depth16_unorm:
        return {1, 1, 2, TextureAspect::depth, false};
    case TextureFormat::depth24_unorm_stencil8:
        return {1, 1, 4, TextureAspect::depth | TextureAspect::stencil, false};
    case TextureFormat::depth32_float:
        return {1, 1, 4, TextureAspect::depth, false};
    case TextureFormat::depth32_float_stencil8:
        return {1, 1, 8, TextureAspect::depth | TextureAspect::stencil, false};
    case TextureFormat::bc1_rgba_unorm:
    case TextureFormat::bc1_rgba_srgb:
        return {4, 4, 8, TextureAspect::color, true};
    case TextureFormat::bc3_rgba_unorm:
    case TextureFormat::bc3_rgba_srgb:
        return {4, 4, 16, TextureAspect::color, true};
    case TextureFormat::unknown:
        break;
    }
    return {};
}

[[nodiscard]] constexpr std::uint32_t mip_dimension(std::uint32_t value,
                                                     std::uint32_t mip) noexcept {
    return std::max(1u, value >> mip);
}

[[nodiscard]] constexpr Extent3D mip_extent(const TextureDesc& desc,
                                             std::uint32_t mip) noexcept {
    return {
        mip_dimension(desc.extent.width, mip),
        mip_dimension(desc.extent.height, mip),
        desc.dimension == TextureDimension::d3
            ? mip_dimension(desc.extent.depth, mip)
            : desc.extent.depth,
    };
}

[[nodiscard]] constexpr std::size_t divide_round_up(std::size_t value,
                                                     std::size_t divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] MemoryRequirements texture_requirements(
    const TextureDesc& desc) noexcept {
    const auto format = format_info(desc.format);
    if (format.bytesPerBlock == 0) {
        return {};
    }
    std::size_t total = 0;
    for (std::uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
        const auto extent = mip_extent(desc, mip);
        const auto blocksX = divide_round_up(extent.width, format.blockWidth);
        const auto blocksY = divide_round_up(extent.height, format.blockHeight);
        const auto layers = desc.dimension == TextureDimension::d3
                                ? extent.depth
                                : desc.arrayLayers;
        if (blocksX > std::numeric_limits<std::size_t>::max() / blocksY ||
            blocksX * blocksY >
                std::numeric_limits<std::size_t>::max() / format.bytesPerBlock ||
            blocksX * blocksY * format.bytesPerBlock >
                std::numeric_limits<std::size_t>::max() / layers) {
            return {};
        }
        const auto mipSize =
            blocksX * blocksY * format.bytesPerBlock * layers;
        if (total > std::numeric_limits<std::size_t>::max() - mipSize) {
            return {};
        }
        total += mipSize;
    }
    return {.size = total, .alignment = format.compressed ? 16u : 4u};
}

struct SubresourceLayout {
    std::size_t offset = 0;
    std::size_t bytesPerRow = 0;
    std::size_t rows = 0;
    std::size_t depth = 0;
};

[[nodiscard]] SubresourceLayout subresource_layout(
    const TextureDesc& desc, const TextureSubresource& subresource) noexcept {
    const auto format = format_info(desc.format);
    std::size_t offset = 0;
    for (std::uint32_t mip = 0; mip < subresource.mipLevel; ++mip) {
        const auto extent = mip_extent(desc, mip);
        const auto rows = divide_round_up(extent.height, format.blockHeight);
        const auto rowBytes =
            divide_round_up(extent.width, format.blockWidth) *
            format.bytesPerBlock;
        const auto layers = desc.dimension == TextureDimension::d3
                                ? extent.depth
                                : desc.arrayLayers;
        offset += rowBytes * rows * layers;
    }
    const auto extent = mip_extent(desc, subresource.mipLevel);
    const auto rows = divide_round_up(extent.height, format.blockHeight);
    const auto rowBytes = divide_round_up(extent.width, format.blockWidth) *
                          format.bytesPerBlock;
    const auto depth = desc.dimension == TextureDimension::d3 ? extent.depth : 1u;
    if (desc.dimension != TextureDimension::d3) {
        offset += rowBytes * rows * subresource.arrayLayer;
    }
    return {offset, rowBytes, rows, depth};
}

[[nodiscard]] bool texture_region_valid(const TextureDesc& desc,
                                        const TextureRegion& region) noexcept {
    if (region.subresource.mipLevel >= desc.mipLevels ||
        region.subresource.arrayLayer >= desc.arrayLayers ||
        !has_aspect(validation::format_aspects(desc.format),
                    region.subresource.aspect) ||
        !validation::is_non_zero(region.extent)) {
        return false;
    }
    const auto extent = mip_extent(desc, region.subresource.mipLevel);
    if (region.origin.x > extent.width ||
        region.extent.width > extent.width - region.origin.x ||
        region.origin.y > extent.height ||
        region.extent.height > extent.height - region.origin.y ||
        region.origin.z > extent.depth ||
        region.extent.depth > extent.depth - region.origin.z) {
        return false;
    }
    if (desc.dimension != TextureDimension::d3 &&
        (region.origin.z != 0 || region.extent.depth != 1)) {
        return false;
    }
    const auto format = format_info(desc.format);
    if (format.compressed) {
        if (region.origin.x % format.blockWidth != 0 ||
            region.origin.y % format.blockHeight != 0) {
            return false;
        }
        const bool widthAligned = region.extent.width % format.blockWidth == 0 ||
                                  region.origin.x + region.extent.width ==
                                      extent.width;
        const bool heightAligned =
            region.extent.height % format.blockHeight == 0 ||
            region.origin.y + region.extent.height == extent.height;
        if (!widthAligned || !heightAligned) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t texture_data_size(
    const TextureDesc& desc, const TextureRegion& region,
    const TextureDataLayout& layout) noexcept {
    if (!texture_region_valid(desc, region)) {
        return 0;
    }
    const auto format = format_info(desc.format);
    const auto rowBytes =
        divide_round_up(region.extent.width, format.blockWidth) *
        format.bytesPerBlock;
    const auto rowCount =
        divide_round_up(region.extent.height, format.blockHeight);
    const auto dataRowBytes = layout.bytesPerRow == 0 ? rowBytes : layout.bytesPerRow;
    const auto rowsPerImage =
        layout.rowsPerImage == 0 ? rowCount : layout.rowsPerImage;
    if (dataRowBytes < rowBytes || rowsPerImage < rowCount) {
        return 0;
    }
    return layout.offset +
           (region.extent.depth - 1u) * dataRowBytes * rowsPerImage +
           (rowCount - 1u) * dataRowBytes + rowBytes;
}

[[nodiscard]] constexpr std::size_t memory_domain_index(
    MemoryDomain domain) noexcept {
    switch (domain) {
    case MemoryDomain::upload:
        return 0;
    case MemoryDomain::readback:
        return 1;
    case MemoryDomain::device_local:
        return 2;
    case MemoryDomain::external:
        return 3;
    }
    return 3;
}

struct MemoryLedger {
    std::array<std::size_t, 4> budgets{};
    std::array<std::size_t, 4> used{};
    ResourceAllocatorCallbacks callbacks;
    mutable std::mutex mutex;

    [[nodiscard]] bool reserve(MemoryDomain domain, std::size_t size,
                               std::size_t alignment) {
        if (callbacks.reserve != nullptr &&
            !callbacks.reserve(domain, size, alignment, callbacks.userData)) {
            return false;
        }
        const auto index = memory_domain_index(domain);
        bool accepted = false;
        {
            std::lock_guard lock{mutex};
            if (size <= budgets[index] && used[index] <= budgets[index] - size) {
                used[index] += size;
                accepted = true;
            }
        }
        if (!accepted && callbacks.release != nullptr) {
            callbacks.release(domain, size, alignment, callbacks.userData);
        }
        return accepted;
    }

    void release(MemoryDomain domain, std::size_t size,
                 std::size_t alignment) noexcept {
        const auto index = memory_domain_index(domain);
        {
            std::lock_guard lock{mutex};
            used[index] = size <= used[index] ? used[index] - size : 0;
        }
        if (callbacks.release != nullptr) {
            callbacks.release(domain, size, alignment, callbacks.userData);
        }
    }

    [[nodiscard]] MemoryBudget budget(MemoryDomain domain) const noexcept {
        const auto index = memory_domain_index(domain);
        std::lock_guard lock{mutex};
        return {.budgetBytes = budgets[index], .usedBytes = used[index]};
    }
};

struct MemoryReservation {
    MemoryReservation(std::shared_ptr<MemoryLedger> ledgerValue,
                      MemoryDomain domainValue, MemoryRequirements requirementsValue)
        : ledger(std::move(ledgerValue)), domain(domainValue),
          requirements(requirementsValue) {}
    ~MemoryReservation() {
        if (ledger) {
            ledger->release(domain, requirements.size, requirements.alignment);
        }
    }
    MemoryReservation(const MemoryReservation&) = delete;
    MemoryReservation& operator=(const MemoryReservation&) = delete;

    std::shared_ptr<MemoryLedger> ledger;
    MemoryDomain domain = MemoryDomain::device_local;
    MemoryRequirements requirements;
};

[[nodiscard]] Result<std::shared_ptr<MemoryReservation>> reserve_memory(
    const std::shared_ptr<MemoryLedger>& ledger, MemoryDomain domain,
    MemoryRequirements requirements, std::string objectName) {
    if (!ledger || !ledger->reserve(domain, requirements.size,
                                    requirements.alignment)) {
        return Status::failure(StatusCode::out_of_memory,
                               std::move(objectName) +
                                   " exceeds its memory budget");
    }
    try {
        return std::make_shared<MemoryReservation>(ledger, domain, requirements);
    } catch (const std::bad_alloc&) {
        ledger->release(domain, requirements.size, requirements.alignment);
        return Status::failure(StatusCode::out_of_memory,
                               std::move(objectName) + " allocation failed");
    }
}

struct AdapterPayload {
    AdapterInfo info;
};

struct DevicePayload {
    AdapterInfo adapter;
    std::vector<Feature> enabledFeatures;
    std::shared_ptr<MemoryLedger> memory;
    std::atomic<bool> lost{false};
};

struct QueuePayload {
    QueueKind kind = QueueKind::graphics;
    std::mutex submitMutex;
};

struct CommandPoolPayload {
    QueueKind kind = QueueKind::graphics;
    std::thread::id owner;
};

struct BufferPayload {
    BufferPayload(BufferDesc value,
                  std::shared_ptr<MemoryReservation> reservationValue,
                  std::shared_ptr<void> nativeValue, bool logical)
        : desc(std::move(value)), reservation(std::move(reservationValue)),
          native(std::move(nativeValue)),
          bytes(logical ? desc.size : 0), mapped(desc.mappedAtCreation) {}
    BufferDesc desc;
    std::shared_ptr<MemoryReservation> reservation;
    std::shared_ptr<void> native;
    std::vector<std::byte> bytes;
    bool mapped = false;
    mutable std::mutex mutex;
};

struct TexturePayload {
    TexturePayload(TextureDesc value,
                   std::shared_ptr<MemoryReservation> reservationValue,
                   std::shared_ptr<void> nativeValue, bool logical)
        : desc(std::move(value)), reservation(std::move(reservationValue)),
          native(std::move(nativeValue)),
          bytes(logical ? texture_requirements(desc).size : 0) {}
    TextureDesc desc;
    std::shared_ptr<MemoryReservation> reservation;
    std::shared_ptr<void> native;
    std::vector<std::byte> bytes;
    mutable std::mutex mutex;
};

[[nodiscard]] Status logical_texture_transfer(
    TexturePayload& texture, const TextureRegion& region,
    std::span<std::byte> mutableData, std::span<const std::byte> constData,
    const TextureDataLayout& layout, bool write) {
    const auto required = texture_data_size(texture.desc, region, layout);
    const auto available = write ? constData.size() : mutableData.size();
    if (required == 0 || available < required) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture data layout or span is too small");
    }
    const auto format = format_info(texture.desc.format);
    const auto subresource = subresource_layout(texture.desc, region.subresource);
    const auto rowBytes =
        divide_round_up(region.extent.width, format.blockWidth) *
        format.bytesPerBlock;
    const auto rowCount =
        divide_round_up(region.extent.height, format.blockHeight);
    const auto dataRowBytes = layout.bytesPerRow == 0 ? rowBytes : layout.bytesPerRow;
    const auto rowsPerImage =
        layout.rowsPerImage == 0 ? rowCount : layout.rowsPerImage;
    const auto originBlockX = region.origin.x / format.blockWidth;
    const auto originBlockY = region.origin.y / format.blockHeight;
    std::lock_guard lock{texture.mutex};
    for (std::uint32_t z = 0; z < region.extent.depth; ++z) {
        for (std::size_t row = 0; row < rowCount; ++row) {
            const auto textureOffset =
                subresource.offset +
                (region.origin.z + z) * subresource.bytesPerRow *
                    subresource.rows +
                (originBlockY + row) * subresource.bytesPerRow +
                originBlockX * format.bytesPerBlock;
            const auto dataOffset = layout.offset +
                                    z * dataRowBytes * rowsPerImage +
                                    row * dataRowBytes;
            if (textureOffset > texture.bytes.size() ||
                rowBytes > texture.bytes.size() - textureOffset) {
                return Status::failure(StatusCode::invalid_argument,
                                       "texture region exceeds allocation");
            }
            if (write) {
                std::copy_n(constData.begin() +
                                static_cast<std::ptrdiff_t>(dataOffset),
                            static_cast<std::ptrdiff_t>(rowBytes),
                            texture.bytes.begin() +
                                static_cast<std::ptrdiff_t>(textureOffset));
            } else {
                std::copy_n(texture.bytes.begin() +
                                static_cast<std::ptrdiff_t>(textureOffset),
                            static_cast<std::ptrdiff_t>(rowBytes),
                            mutableData.begin() +
                                static_cast<std::ptrdiff_t>(dataOffset));
            }
        }
    }
    return Status::success();
}

struct BufferViewPayload {
    BufferViewDesc desc;
    Handle bufferHandle = 0;
    std::shared_ptr<BufferPayload> buffer;
};

struct TextureViewPayload {
    TextureViewDesc desc;
    Handle textureHandle = 0;
    std::shared_ptr<TexturePayload> texture;
    std::shared_ptr<void> native;
};

enum class TransferKind {
    copy_buffer,
    fill_buffer,
    copy_buffer_to_texture,
    copy_texture_to_buffer,
    copy_texture,
    clear_texture,
    resolve_texture,
    blit_texture,
};

struct TransferCommand {
    TransferKind kind = TransferKind::copy_buffer;
    std::shared_ptr<BufferPayload> sourceBuffer;
    std::shared_ptr<BufferPayload> destinationBuffer;
    std::shared_ptr<TexturePayload> sourceTexture;
    std::shared_ptr<TexturePayload> destinationTexture;
    BufferCopyRegion buffer;
    BufferTextureCopyRegion bufferTexture;
    TextureCopyRegion texture;
    TextureBlitRegion blit;
    ClearValue clear;
    std::byte fillValue{};
};

[[nodiscard]] bool buffer_range_valid(const BufferPayload& buffer,
                                      std::size_t offset,
                                      std::size_t size) noexcept {
    return size != 0 && offset <= buffer.desc.size &&
           size <= buffer.desc.size - offset;
}

[[nodiscard]] Status logical_copy_buffer(const TransferCommand& command) {
    auto& source = *command.sourceBuffer;
    auto& destination = *command.destinationBuffer;
    const auto& region = command.buffer;
    if (!buffer_range_valid(source, region.sourceOffset, region.size) ||
        !buffer_range_valid(destination, region.destinationOffset, region.size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer copy range exceeds an allocation");
    }
    if (&source == &destination) {
        std::lock_guard lock{source.mutex};
        std::memmove(source.bytes.data() + region.destinationOffset,
                     source.bytes.data() + region.sourceOffset, region.size);
        return Status::success();
    }
    std::scoped_lock lock{source.mutex, destination.mutex};
    std::copy_n(source.bytes.begin() +
                    static_cast<std::ptrdiff_t>(region.sourceOffset),
                static_cast<std::ptrdiff_t>(region.size),
                destination.bytes.begin() +
                    static_cast<std::ptrdiff_t>(region.destinationOffset));
    return Status::success();
}

[[nodiscard]] Status logical_fill_buffer(const TransferCommand& command) {
    auto& destination = *command.destinationBuffer;
    const auto& region = command.buffer;
    if (!buffer_range_valid(destination, region.destinationOffset, region.size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer fill range exceeds the allocation");
    }
    std::lock_guard lock{destination.mutex};
    std::fill_n(destination.bytes.begin() +
                    static_cast<std::ptrdiff_t>(region.destinationOffset),
                static_cast<std::ptrdiff_t>(region.size), command.fillValue);
    return Status::success();
}

[[nodiscard]] TextureDataLayout buffer_texture_layout(
    const BufferTextureCopyRegion& region) noexcept {
    auto layout = region.layout;
    layout.offset += region.bufferOffset;
    return layout;
}

[[nodiscard]] Status logical_copy_buffer_to_texture(
    const TransferCommand& command) {
    auto& source = *command.sourceBuffer;
    auto& destination = *command.destinationTexture;
    const auto layout = buffer_texture_layout(command.bufferTexture);
    const auto required =
        texture_data_size(destination.desc, command.bufferTexture.texture, layout);
    if (required == 0 || required > source.desc.size) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer-to-texture layout exceeds the source buffer");
    }
    std::lock_guard sourceLock{source.mutex};
    return logical_texture_transfer(destination, command.bufferTexture.texture,
                                    {}, source.bytes, layout, true);
}

[[nodiscard]] Status logical_copy_texture_to_buffer(
    const TransferCommand& command) {
    auto& source = *command.sourceTexture;
    auto& destination = *command.destinationBuffer;
    const auto layout = buffer_texture_layout(command.bufferTexture);
    const auto required =
        texture_data_size(source.desc, command.bufferTexture.texture, layout);
    if (required == 0 || required > destination.desc.size) {
        return Status::failure(
            StatusCode::invalid_argument,
            "texture-to-buffer layout exceeds the destination buffer");
    }
    std::lock_guard destinationLock{destination.mutex};
    return logical_texture_transfer(source, command.bufferTexture.texture,
                                    destination.bytes, {}, layout, false);
}

[[nodiscard]] Status logical_copy_texture(const TransferCommand& command,
                                          bool resolve = false) {
    auto& source = *command.sourceTexture;
    auto& destination = *command.destinationTexture;
    if (source.desc.format != destination.desc.format ||
        (!resolve && source.desc.sampleCount != destination.desc.sampleCount) ||
        command.texture.source.extent.width !=
            command.texture.destination.extent.width ||
        command.texture.source.extent.height !=
            command.texture.destination.extent.height ||
        command.texture.source.extent.depth !=
            command.texture.destination.extent.depth) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture copy regions are incompatible");
    }
    const TextureDataLayout tight{};
    const auto size = texture_data_size(source.desc, command.texture.source, tight);
    if (size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture copy region is invalid");
    }
    std::vector<std::byte> bytes(size);
    if (auto status = logical_texture_transfer(
            source, command.texture.source, bytes, {}, tight, false);
        !status.ok()) {
        return status;
    }
    return logical_texture_transfer(destination, command.texture.destination, {},
                                    bytes, tight, true);
}

[[nodiscard]] std::uint16_t float_to_half(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16u) & 0x8000u);
    auto exponent = static_cast<std::int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    auto mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;
        }
        mantissa = (mantissa | 0x800000u) >>
                   static_cast<std::uint32_t>(1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13u));
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint16_t>(exponent) << 10u) |
        static_cast<std::uint16_t>((mantissa + 0x1000u) >> 13u));
}

[[nodiscard]] Status logical_clear_texture(const TransferCommand& command) {
    auto& destination = *command.destinationTexture;
    const auto format = format_info(destination.desc.format);
    if (format.compressed || format.bytesPerBlock == 0) {
        return Status::failure(StatusCode::unsupported,
                               "compressed texture clear is unsupported");
    }
    const TextureDataLayout tight{};
    const auto size =
        texture_data_size(destination.desc, command.texture.destination, tight);
    if (size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture clear region is invalid");
    }
    std::vector<std::byte> bytes(size);
    const auto to_byte = [](float value) {
        return std::byte{static_cast<unsigned char>(
            std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F)};
    };
    std::array<std::byte, 16> pixel{};
    const auto& color = command.clear.color;
    switch (destination.desc.format) {
    case TextureFormat::r8_unorm:
        pixel[0] = to_byte(color.r);
        break;
    case TextureFormat::rg8_unorm:
        pixel[0] = to_byte(color.r);
        pixel[1] = to_byte(color.g);
        break;
    case TextureFormat::rgba8_unorm:
    case TextureFormat::rgba8_srgb:
        pixel[0] = to_byte(color.r);
        pixel[1] = to_byte(color.g);
        pixel[2] = to_byte(color.b);
        pixel[3] = to_byte(color.a);
        break;
    case TextureFormat::bgra8_unorm:
    case TextureFormat::bgra8_srgb:
        pixel[0] = to_byte(color.b);
        pixel[1] = to_byte(color.g);
        pixel[2] = to_byte(color.r);
        pixel[3] = to_byte(color.a);
        break;
    case TextureFormat::rgba16_float: {
        const std::array<std::uint16_t, 4> halves{
            float_to_half(color.r), float_to_half(color.g),
            float_to_half(color.b), float_to_half(color.a)};
        std::memcpy(pixel.data(), halves.data(), sizeof(halves));
        break;
    }
    case TextureFormat::rgba32_float: {
        const std::array<float, 4> floats{color.r, color.g, color.b, color.a};
        std::memcpy(pixel.data(), floats.data(), sizeof(floats));
        break;
    }
    case TextureFormat::depth16_unorm: {
        const auto depth = static_cast<std::uint16_t>(
            std::clamp(command.clear.depth, 0.0F, 1.0F) * 65535.0F + 0.5F);
        std::memcpy(pixel.data(), &depth, sizeof(depth));
        break;
    }
    case TextureFormat::depth24_unorm_stencil8: {
        const auto depth = static_cast<std::uint32_t>(
            std::clamp(command.clear.depth, 0.0F, 1.0F) * 16777215.0F + 0.5F);
        pixel[0] = std::byte{static_cast<unsigned char>(depth & 0xffu)};
        pixel[1] = std::byte{static_cast<unsigned char>((depth >> 8u) & 0xffu)};
        pixel[2] = std::byte{static_cast<unsigned char>((depth >> 16u) & 0xffu)};
        pixel[3] =
            std::byte{static_cast<unsigned char>(command.clear.stencil & 0xffu)};
        break;
    }
    case TextureFormat::depth32_float:
        std::memcpy(pixel.data(), &command.clear.depth, sizeof(float));
        break;
    case TextureFormat::depth32_float_stencil8:
        std::memcpy(pixel.data(), &command.clear.depth, sizeof(float));
        std::memcpy(pixel.data() + sizeof(float), &command.clear.stencil,
                    sizeof(std::uint32_t));
        break;
    case TextureFormat::bc1_rgba_unorm:
    case TextureFormat::bc1_rgba_srgb:
    case TextureFormat::bc3_rgba_unorm:
    case TextureFormat::bc3_rgba_srgb:
    case TextureFormat::unknown:
        return Status::failure(StatusCode::unsupported,
                               "this texture clear format is unsupported");
    }
    const auto pixelSize = format.bytesPerBlock;
    for (std::size_t offset = 0; offset < bytes.size(); offset += pixelSize) {
        std::copy_n(pixel.begin(), static_cast<std::ptrdiff_t>(pixelSize),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return logical_texture_transfer(destination, command.texture.destination, {},
                                    bytes, tight, true);
}

[[nodiscard]] Status logical_blit_texture(const TransferCommand& command) {
    auto& source = *command.sourceTexture;
    auto& destination = *command.destinationTexture;
    const auto info = format_info(source.desc.format);
    if (command.blit.filter != Filter::nearest || info.compressed ||
        !has_aspect(info.aspects, TextureAspect::color) ||
        source.desc.format != destination.desc.format ||
        source.desc.sampleCount != 1 || destination.desc.sampleCount != 1 ||
        command.blit.source.extent.depth != 1 ||
        command.blit.destination.extent.depth != 1) {
        return Status::failure(StatusCode::unsupported,
                               "requested texture blit mode is unsupported");
    }
    const TextureDataLayout tight{};
    const auto sourceSize = texture_data_size(source.desc, command.blit.source, tight);
    const auto destinationSize =
        texture_data_size(destination.desc, command.blit.destination, tight);
    if (sourceSize == 0 || destinationSize == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture blit region is invalid");
    }
    std::vector<std::byte> sourceBytes(sourceSize);
    std::vector<std::byte> destinationBytes(destinationSize);
    if (auto status = logical_texture_transfer(source, command.blit.source,
                                               sourceBytes, {}, tight, false);
        !status.ok()) {
        return status;
    }
    const auto pixelSize = info.bytesPerBlock;
    const auto sourceWidth = command.blit.source.extent.width;
    const auto sourceHeight = command.blit.source.extent.height;
    const auto destinationWidth = command.blit.destination.extent.width;
    const auto destinationHeight = command.blit.destination.extent.height;
    for (std::uint32_t y = 0; y < destinationHeight; ++y) {
        const auto sourceY = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(y) * sourceHeight / destinationHeight);
        for (std::uint32_t x = 0; x < destinationWidth; ++x) {
            const auto sourceX = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(x) * sourceWidth / destinationWidth);
            const auto sourceOffset =
                (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) *
                pixelSize;
            const auto destinationOffset =
                (static_cast<std::size_t>(y) * destinationWidth + x) * pixelSize;
            std::copy_n(sourceBytes.begin() +
                            static_cast<std::ptrdiff_t>(sourceOffset),
                        static_cast<std::ptrdiff_t>(pixelSize),
                        destinationBytes.begin() +
                            static_cast<std::ptrdiff_t>(destinationOffset));
        }
    }
    return logical_texture_transfer(destination, command.blit.destination, {},
                                    destinationBytes, tight, true);
}

[[nodiscard]] Status execute_logical_transfer(const TransferCommand& command) {
    switch (command.kind) {
    case TransferKind::copy_buffer:
        return logical_copy_buffer(command);
    case TransferKind::fill_buffer:
        return logical_fill_buffer(command);
    case TransferKind::copy_buffer_to_texture:
        return logical_copy_buffer_to_texture(command);
    case TransferKind::copy_texture_to_buffer:
        return logical_copy_texture_to_buffer(command);
    case TransferKind::copy_texture:
        return logical_copy_texture(command);
    case TransferKind::clear_texture:
        return logical_clear_texture(command);
    case TransferKind::resolve_texture:
        return logical_copy_texture(command, true);
    case TransferKind::blit_texture:
        return logical_blit_texture(command);
    }
    return Status::failure(StatusCode::invalid_argument,
                           "unknown logical transfer command");
}

[[nodiscard]] NativeTransfer make_native_transfer(
    const TransferCommand& command) {
    NativeTransfer native;
    native.kind = static_cast<NativeTransferKind>(command.kind);
    native.source = command.sourceBuffer ? command.sourceBuffer->native
                                         : command.sourceTexture
                                               ? command.sourceTexture->native
                                               : nullptr;
    native.destination = command.destinationBuffer
                             ? command.destinationBuffer->native
                             : command.destinationTexture
                                   ? command.destinationTexture->native
                                   : nullptr;
    native.buffer = command.buffer;
    native.bufferTexture = command.bufferTexture;
    native.texture = command.texture;
    native.blit = command.blit;
    native.clear = command.clear;
    native.fillValue = command.fillValue;
    return native;
}

struct CommandListPayload {
    QueueKind kind = QueueKind::graphics;
    CommandListState state = CommandListState::initial;
    std::thread::id owner;
    std::uint32_t activeEncoder = 0;
    bool graphicsPipelineBound = false;
    bool computePipelineBound = false;
    std::vector<std::shared_ptr<void>> retained;
    std::vector<TransferCommand> transfers;
    std::mutex mutex;
};

struct ShaderPayload {
    explicit ShaderPayload(ShaderDesc value)
        : desc(std::move(value)), reflection(desc.reflection) {}
    ShaderDesc desc;
    PipelineReflection reflection;
};

struct PipelinePayload {
    PipelineReflection reflection;
};

struct ComputePipelinePayload {
    PipelineReflection reflection;
    Extent3D preferredWorkgroupSize{64, 1, 1};
};

struct FencePayload {
    explicit FencePayload(std::uint64_t initial) : value(initial) {}
    std::uint64_t value = 0;
    std::mutex mutex;
    std::condition_variable changed;
};

struct SemaphorePayload {
    explicit SemaphorePayload(std::uint64_t initial) : value(initial) {}
    std::atomic<std::uint64_t> value{0};
};

struct QueryPoolPayload {
    QueryPoolDesc desc;
};

struct SurfacePayload {
    SurfaceDesc desc;
};

struct SwapchainPayload {
    SwapchainDesc desc;
    std::shared_ptr<TexturePayload> image;
    std::uint32_t nextImage = 0;
    bool acquired = false;
    mutable std::mutex mutex;
};

struct UploadRingPayload {
    std::vector<std::shared_ptr<BufferPayload>> frames;
    std::uint32_t currentFrame = 0;
    std::size_t offset = 0;
    std::size_t bytesPerFrame = 0;
    std::mutex mutex;
};

struct Runtime : std::enable_shared_from_this<Runtime> {
    struct Slot {
        std::uint32_t generation = 1;
        ObjectKind kind = ObjectKind::instance;
        std::shared_ptr<void> payload;
        bool occupied = false;
    };

    InstanceDesc instanceDesc;
    FoundationBackendConfig config;
    const BackendDispatch* dispatch = nullptr;
    mutable std::mutex mutex;
    std::vector<Slot> slots;
    BackendStats stats;

    [[nodiscard]] Handle allocate(ObjectKind kind, std::shared_ptr<void> payload) {
        std::lock_guard lock{mutex};
        for (std::size_t index = 0; index < slots.size(); ++index) {
            auto& slot = slots[index];
            if (slot.occupied) {
                continue;
            }
            slot.kind = kind;
            slot.payload = std::move(payload);
            slot.occupied = true;
            return (static_cast<Handle>(slot.generation) << 32u) |
                   static_cast<Handle>(index + 1);
        }
        slots.push_back(Slot{1, kind, std::move(payload), true});
        return (static_cast<Handle>(1) << 32u) |
               static_cast<Handle>(slots.size());
    }

    [[nodiscard]] bool valid(ObjectKind kind, Handle handle) const noexcept {
        if (handle == 0) {
            return false;
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        return index < slots.size() && slots[index].occupied &&
               slots[index].generation == generation && slots[index].kind == kind;
    }

    template <typename T>
    [[nodiscard]] std::shared_ptr<T> resolve(ObjectKind kind, Handle handle) const {
        if (handle == 0) {
            return {};
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return {};
        }
        const auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return {};
        }
        return std::static_pointer_cast<T>(slot.payload);
    }

    [[nodiscard]] std::shared_ptr<void> retain(ObjectKind kind, Handle handle) const {
        if (handle == 0) {
            return {};
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return {};
        }
        const auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return {};
        }
        return slot.payload;
    }

    void release(ObjectKind kind, Handle handle) noexcept {
        if (handle == 0) {
            return;
        }
        const auto index = static_cast<std::uint32_t>(handle) - 1u;
        const auto generation = static_cast<std::uint32_t>(handle >> 32u);
        std::lock_guard lock{mutex};
        if (index >= slots.size()) {
            return;
        }
        auto& slot = slots[index];
        if (!slot.occupied || slot.generation != generation || slot.kind != kind) {
            return;
        }
        slot.payload.reset();
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
    }

    template <typename Function>
    void update_stats(Function&& function) {
        std::lock_guard lock{mutex};
        function(stats);
    }

    [[nodiscard]] BackendStats stats_snapshot() const noexcept {
        std::lock_guard lock{mutex};
        return stats;
    }
};

struct ObjectState {
    ObjectState(std::shared_ptr<Runtime> runtimeValue, ObjectKind kindValue,
                Handle handleValue)
        : runtime(std::move(runtimeValue)), kind(kindValue), handle(handleValue) {}

    ~ObjectState() {
        if (runtime) {
            runtime->release(kind, handle);
        }
    }

    std::shared_ptr<Runtime> runtime;
    ObjectKind kind = ObjectKind::instance;
    Handle handle = 0;
};

struct SwapchainState {
    explicit SwapchainState(std::unique_ptr<ObjectState> objectValue)
        : object(std::move(objectValue)) {}
    std::unique_ptr<ObjectState> object;
    std::unique_ptr<Texture> image;
    std::unique_ptr<Semaphore> available;
};

struct UploadRingState {
    explicit UploadRingState(std::unique_ptr<ObjectState> objectValue)
        : object(std::move(objectValue)) {}
    std::unique_ptr<ObjectState> object;
    std::vector<Buffer> buffers;
};

[[nodiscard]] Status invalid_object(std::string object) {
    return Status::failure(StatusCode::invalid_state,
                           std::move(object) + " is invalid or stale");
}

[[nodiscard]] Status unsupported(Runtime& runtime, std::string operation) {
    return Status::failure(
        StatusCode::unsupported,
        std::move(operation) + " is not implemented by the " +
            runtime.config.adapterName + " RHI 1 foundation");
}

[[nodiscard]] std::unique_ptr<ObjectState> make_state(
    const std::shared_ptr<Runtime>& runtime, ObjectKind kind, Handle handle) {
    return std::make_unique<ObjectState>(runtime, kind, handle);
}

struct Factory {
    [[nodiscard]] static Instance instance(const std::shared_ptr<Runtime>& runtime,
                                           Handle handle) {
        return Instance{make_state(runtime, ObjectKind::instance, handle)};
    }
    [[nodiscard]] static Adapter adapter(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Adapter{make_state(runtime, ObjectKind::adapter, handle)};
    }
    [[nodiscard]] static Device device(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Device{make_state(runtime, ObjectKind::device, handle)};
    }
    [[nodiscard]] static Queue queue(const std::shared_ptr<Runtime>& runtime,
                                     Handle handle) {
        return Queue{make_state(runtime, ObjectKind::queue, handle)};
    }
    [[nodiscard]] static CommandPool command_pool(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return CommandPool{make_state(runtime, ObjectKind::command_pool, handle)};
    }
    [[nodiscard]] static CommandList command_list(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return CommandList{make_state(runtime, ObjectKind::command_list, handle)};
    }
    [[nodiscard]] static Buffer buffer(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Buffer{make_state(runtime, ObjectKind::buffer, handle)};
    }
    [[nodiscard]] static BufferView buffer_view(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return BufferView{make_state(runtime, ObjectKind::buffer_view, handle)};
    }
    [[nodiscard]] static Texture texture(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Texture{make_state(runtime, ObjectKind::texture, handle)};
    }
    [[nodiscard]] static TextureView texture_view(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return TextureView{make_state(runtime, ObjectKind::texture_view, handle)};
    }
    [[nodiscard]] static Shader shader(const std::shared_ptr<Runtime>& runtime,
                                       Handle handle) {
        return Shader{make_state(runtime, ObjectKind::shader, handle)};
    }
    [[nodiscard]] static Pipeline pipeline(const std::shared_ptr<Runtime>& runtime,
                                           Handle handle) {
        return Pipeline{make_state(runtime, ObjectKind::pipeline, handle)};
    }
    [[nodiscard]] static ComputePipeline compute_pipeline(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return ComputePipeline{
            make_state(runtime, ObjectKind::compute_pipeline, handle)};
    }
    [[nodiscard]] static Fence fence(const std::shared_ptr<Runtime>& runtime,
                                     Handle handle) {
        return Fence{make_state(runtime, ObjectKind::fence, handle)};
    }
    [[nodiscard]] static Semaphore semaphore(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return Semaphore{make_state(runtime, ObjectKind::semaphore, handle)};
    }
    [[nodiscard]] static QueryPool query_pool(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return QueryPool{make_state(runtime, ObjectKind::query_pool, handle)};
    }
    [[nodiscard]] static Surface surface(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Surface{make_state(runtime, ObjectKind::surface, handle)};
    }
    [[nodiscard]] static Swapchain swapchain(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return Swapchain{std::make_unique<SwapchainState>(
            make_state(runtime, ObjectKind::swapchain, handle))};
    }
    [[nodiscard]] static UploadRing upload_ring(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        auto state = std::make_unique<UploadRingState>(
            make_state(runtime, ObjectKind::upload_ring, handle));
        const auto payload = runtime->resolve<UploadRingPayload>(
            ObjectKind::upload_ring, handle);
        if (payload) {
            state->buffers.reserve(payload->frames.size());
            for (const auto& frame : payload->frames) {
                const auto bufferHandle = runtime->allocate(ObjectKind::buffer, frame);
                state->buffers.push_back(buffer(runtime, bufferHandle));
            }
        }
        return UploadRing{std::move(state)};
    }
};

[[nodiscard]] Result<Handle> foundation_create_adapter(Runtime& runtime,
                                                       std::size_t index) {
    if (index != 0 || runtime.config.adapterName.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "adapter index is out of range");
    }
    AdapterInfo info;
    info.name = runtime.config.adapterName;
    info.backend = runtime.config.kind;
    info.native = runtime.config.native;
    info.validationOnly = runtime.config.validationOnly;
    info.presentation = runtime.config.presentation;
    info.queueKinds = runtime.config.queueKinds;
    info.supportedFeatures = runtime.config.supportedFeatures;
    info.resources = runtime.config.resourceCapabilities;
    return runtime.allocate(ObjectKind::adapter,
                            std::make_shared<AdapterPayload>(
                                AdapterPayload{std::move(info)}));
}

[[nodiscard]] Result<Handle> foundation_create_device(Runtime& runtime,
                                                      Handle adapterHandle,
                                                      const DeviceDesc& desc) {
    const auto adapter = runtime.resolve<AdapterPayload>(ObjectKind::adapter,
                                                         adapterHandle);
    if (!adapter) {
        return invalid_object("adapter");
    }
    std::vector<Feature> enabled = desc.requiredFeatures;
    for (const auto feature : desc.requiredFeatures) {
        if (!validation::supports_feature(adapter->info, feature)) {
            return Status::failure(StatusCode::unsupported,
                                   "required device feature is unsupported");
        }
    }
    for (const auto feature : desc.optionalFeatures) {
        if (validation::supports_feature(adapter->info, feature) &&
            std::find(enabled.begin(), enabled.end(), feature) == enabled.end()) {
            enabled.push_back(feature);
        }
    }
    auto payload = std::make_shared<DevicePayload>();
    payload->adapter = adapter->info;
    payload->enabledFeatures = std::move(enabled);
    payload->memory = std::make_shared<MemoryLedger>();
    payload->memory->budgets = {
        runtime.config.uploadBudgetBytes,
        runtime.config.readbackBudgetBytes,
        runtime.config.deviceLocalBudgetBytes,
        runtime.config.deviceLocalBudgetBytes,
    };
    payload->memory->callbacks = desc.allocator;
    runtime.update_stats([](BackendStats& stats) { ++stats.devicesCreated; });
    return runtime.allocate(ObjectKind::device, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_queue(Runtime& runtime,
                                                     Handle deviceHandle,
                                                     QueueKind kind) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::supports_queue(device->adapter, kind)) {
        return Status::failure(StatusCode::unsupported,
                               "requested queue kind is unsupported");
    }
    auto payload = std::make_shared<QueuePayload>();
    payload->kind = kind;
    return runtime.allocate(ObjectKind::queue, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_command_pool(Runtime& runtime,
                                                            Handle deviceHandle,
                                                            QueueKind kind) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::supports_queue(device->adapter, kind)) {
        return Status::failure(StatusCode::unsupported,
                               "requested queue kind is unsupported");
    }
    auto payload = std::make_shared<CommandPoolPayload>();
    payload->kind = kind;
    payload->owner = std::this_thread::get_id();
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.commandPoolsCreated; });
    return runtime.allocate(ObjectKind::command_pool, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_allocate_command_list(Runtime& runtime,
                                                              Handle poolHandle) {
    const auto pool = runtime.resolve<CommandPoolPayload>(ObjectKind::command_pool,
                                                          poolHandle);
    if (!pool) {
        return invalid_object("command pool");
    }
    if (pool->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command pool is owned by another thread");
    }
    auto payload = std::make_shared<CommandListPayload>();
    payload->kind = pool->kind;
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.commandListsCreated; });
    return runtime.allocate(ObjectKind::command_list, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_buffer(Runtime& runtime,
                                                      Handle deviceHandle,
                                                      const BufferDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::buffer_desc_valid(desc)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer descriptor is invalid");
    }
    if (desc.memory == MemoryDomain::external) {
        return Status::failure(StatusCode::invalid_argument,
                               "external memory requires import_buffer");
    }
    if (desc.shareable && !device->adapter.resources.externalExport) {
        return unsupported(runtime, "shareable buffer creation");
    }
    const MemoryRequirements requirements{desc.size, 16};
    auto reservationResult = reserve_memory(device->memory, desc.memory,
                                            requirements, "buffer allocation");
    if (!reservationResult.ok()) {
        return reservationResult.status();
    }
    auto reservation = std::move(reservationResult).value();
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createBuffer == nullptr) {
            return unsupported(runtime, "buffer creation");
        }
        auto nativeResult = runtime.config.createBuffer(desc);
        if (!nativeResult.ok()) {
            return nativeResult.status();
        }
        native = std::move(nativeResult).value();
    }
    try {
        const auto handle = runtime.allocate(
            ObjectKind::buffer,
            std::make_shared<BufferPayload>(desc, std::move(reservation),
                                            std::move(native),
                                            runtime.config.logicalResources));
        runtime.update_stats([](BackendStats& stats) { ++stats.buffersCreated; });
        return handle;
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "buffer allocation failed");
    }
}

[[nodiscard]] Result<Handle> foundation_create_buffer_view(
    Runtime& runtime, Handle deviceHandle, Handle bufferHandle,
    const BufferViewDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    const auto buffer = runtime.resolve<BufferPayload>(ObjectKind::buffer,
                                                       bufferHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!buffer) {
        return invalid_object("buffer");
    }
    if (!device->adapter.resources.bufferViews) {
        return unsupported(runtime, "buffer view creation");
    }
    BufferViewDesc normalized = desc;
    if (normalized.size == whole_size) {
        normalized.size = buffer->desc.size -
                          std::min(buffer->desc.size, normalized.offset);
    }
    if (normalized.offset > buffer->desc.size || normalized.size == 0 ||
        normalized.size > buffer->desc.size - normalized.offset ||
        (normalized.stride != 0 &&
         normalized.size % normalized.stride != 0)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer view range or stride is invalid");
    }
    auto payload = std::make_shared<BufferViewPayload>();
    payload->desc = std::move(normalized);
    payload->bufferHandle = bufferHandle;
    payload->buffer = buffer;
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.bufferViewsCreated; });
    return runtime.allocate(ObjectKind::buffer_view, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_texture(Runtime& runtime,
                                                       Handle deviceHandle,
                                                       const TextureDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::texture_desc_valid(desc)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture descriptor is invalid");
    }
    if (desc.memory == MemoryDomain::external) {
        return Status::failure(StatusCode::invalid_argument,
                               "external memory requires import_texture");
    }
    if (desc.shareable && !device->adapter.resources.externalExport) {
        return unsupported(runtime, "shareable texture creation");
    }
    const auto requirements = texture_requirements(desc);
    if (requirements.size == 0) {
        return Status::failure(StatusCode::out_of_memory,
                               "texture allocation size overflowed");
    }
    auto reservationResult = reserve_memory(device->memory, desc.memory,
                                            requirements, "texture allocation");
    if (!reservationResult.ok()) {
        return reservationResult.status();
    }
    auto reservation = std::move(reservationResult).value();
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createTexture == nullptr) {
            return unsupported(runtime, "texture creation");
        }
        auto nativeResult = runtime.config.createTexture(desc);
        if (!nativeResult.ok()) {
            return nativeResult.status();
        }
        native = std::move(nativeResult).value();
    }
    try {
        const auto handle = runtime.allocate(
            ObjectKind::texture,
            std::make_shared<TexturePayload>(desc, std::move(reservation),
                                             std::move(native),
                                             runtime.config.logicalResources));
        runtime.update_stats([](BackendStats& stats) { ++stats.texturesCreated; });
        return handle;
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "texture allocation failed");
    }
}

[[nodiscard]] Result<Handle> foundation_create_texture_view(
    Runtime& runtime, Handle deviceHandle, Handle textureHandle,
    const TextureViewDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    const auto texture = runtime.resolve<TexturePayload>(ObjectKind::texture,
                                                         textureHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!texture) {
        return invalid_object("texture");
    }
    if (!device->adapter.resources.textureViews) {
        return unsupported(runtime, "texture view creation");
    }
    TextureViewDesc normalized = desc;
    if (normalized.format == TextureFormat::unknown) {
        normalized.format = texture->desc.format;
    }
    if (!validation::texture_view_desc_valid(texture->desc, normalized)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture view descriptor is invalid");
    }
    auto payload = std::make_shared<TextureViewPayload>();
    payload->desc = std::move(normalized);
    payload->textureHandle = textureHandle;
    payload->texture = texture;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createTextureView == nullptr) {
            return unsupported(runtime, "native texture view creation");
        }
        auto nativeView =
            runtime.config.createTextureView(texture->native, payload->desc);
        if (!nativeView.ok()) {
            return nativeView.status();
        }
        payload->native = std::move(nativeView).value();
    }
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.textureViewsCreated; });
    return runtime.allocate(ObjectKind::texture_view, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_import_buffer(
    Runtime& runtime, Handle deviceHandle, const BufferDesc& desc,
    ExternalMemoryHandle handle) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::buffer_desc_valid(desc) || !handle.valid()) {
        return Status::failure(StatusCode::invalid_argument,
                               "external buffer descriptor or handle is invalid");
    }
    return unsupported(runtime, "external buffer import");
}

[[nodiscard]] Result<Handle> foundation_import_texture(
    Runtime& runtime, Handle deviceHandle, const TextureDesc& desc,
    ExternalMemoryHandle handle) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!validation::texture_desc_valid(desc) || !handle.valid()) {
        return Status::failure(StatusCode::invalid_argument,
                               "external texture descriptor or handle is invalid");
    }
    return unsupported(runtime, "external texture import");
}

[[nodiscard]] Result<Handle> foundation_create_shader(Runtime& runtime,
                                                      Handle deviceHandle,
                                                      const ShaderDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "shader creation");
    }
    if (desc.entryPoint.empty() || desc.code.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "shader entry point and code are required");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.shadersCreated; });
    return runtime.allocate(ObjectKind::shader,
                            std::make_shared<ShaderPayload>(desc));
}

[[nodiscard]] std::vector<ResourceBinding> merge_reflection(
    const std::shared_ptr<ShaderPayload>& first,
    const std::shared_ptr<ShaderPayload>& second = {}) {
    std::vector<ResourceBinding> bindings;
    if (first) {
        const auto reflected = first->reflection.bindings();
        bindings.insert(bindings.end(), reflected.begin(), reflected.end());
    }
    if (second) {
        const auto reflected = second->reflection.bindings();
        bindings.insert(bindings.end(), reflected.begin(), reflected.end());
    }
    return bindings;
}

[[nodiscard]] Result<Handle> foundation_create_pipeline(Runtime& runtime,
                                                        Handle deviceHandle,
                                                        const PipelineDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "graphics pipeline creation");
    }
    std::shared_ptr<ShaderPayload> vertex;
    std::shared_ptr<ShaderPayload> fragment;
    if (desc.vertexShader != nullptr) {
        if (!desc.vertexShader->valid()) {
            return invalid_object("vertex shader");
        }
        vertex = runtime.resolve<ShaderPayload>(ObjectKind::shader,
                                                desc.vertexShader->id().value);
        if (!vertex || vertex->desc.stage != ShaderStage::vertex) {
            return Status::failure(StatusCode::invalid_argument,
                                   "vertex pipeline input must be a vertex shader");
        }
    }
    if (desc.fragmentShader != nullptr) {
        if (!desc.fragmentShader->valid()) {
            return invalid_object("fragment shader");
        }
        fragment = runtime.resolve<ShaderPayload>(ObjectKind::shader,
                                                  desc.fragmentShader->id().value);
        if (!fragment || fragment->desc.stage != ShaderStage::fragment) {
            return Status::failure(
                StatusCode::invalid_argument,
                "fragment pipeline input must be a fragment shader");
        }
    }
    auto payload = std::make_shared<PipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(vertex, fragment)};
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_compute_pipeline(
    Runtime& runtime, Handle deviceHandle, const ComputePipelineDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "compute pipeline creation");
    }
    if (desc.computeShader == nullptr || !desc.computeShader->valid()) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline requires a valid shader");
    }
    const auto shader = runtime.resolve<ShaderPayload>(
        ObjectKind::shader, desc.computeShader->id().value);
    if (!shader || shader->desc.stage != ShaderStage::compute) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline requires a compute shader");
    }
    auto payload = std::make_shared<ComputePipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(shader)};
    payload->preferredWorkgroupSize = desc.preferredWorkgroupSize;
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::compute_pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_fence(Runtime& runtime,
                                                     Handle deviceHandle,
                                                     const FenceDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "fence creation");
    }
    return runtime.allocate(ObjectKind::fence,
                            std::make_shared<FencePayload>(desc.initialValue));
}

[[nodiscard]] Result<Handle> foundation_create_semaphore(
    Runtime& runtime, Handle deviceHandle, const SemaphoreDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "semaphore creation");
    }
    return runtime.allocate(
        ObjectKind::semaphore,
        std::make_shared<SemaphorePayload>(desc.initialValue));
}

[[nodiscard]] Result<Handle> foundation_create_query_pool(
    Runtime& runtime, Handle deviceHandle, const QueryPoolDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "query pool creation");
    }
    if (desc.count == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "query pool count must be non-zero");
    }
    return runtime.allocate(ObjectKind::query_pool,
                            std::make_shared<QueryPoolPayload>(
                                QueryPoolPayload{desc}));
}

[[nodiscard]] Result<Handle> foundation_create_surface(Runtime& runtime,
                                                       Handle deviceHandle,
                                                       const SurfaceDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (!validation::is_non_zero(desc.initialExtent) ||
        !validation::native_surface_handles_valid(desc.native)) {
        return Status::failure(StatusCode::invalid_argument,
                               "surface descriptor is invalid");
    }
    if (!runtime.config.presentation) {
        return unsupported(runtime, "surface creation");
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.surfacesCreated; });
    return runtime.allocate(ObjectKind::surface,
                            std::make_shared<SurfacePayload>(
                                SurfacePayload{desc}));
}

[[nodiscard]] Result<Handle> foundation_create_swapchain(
    Runtime& runtime, Handle deviceHandle, Handle surfaceHandle,
    const SwapchainDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!runtime.resolve<SurfacePayload>(ObjectKind::surface, surfaceHandle)) {
        return invalid_object("surface");
    }
    if (!validation::is_non_zero(desc.extent) || desc.imageCount == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "swapchain extent and image count must be non-zero");
    }
    auto payload = std::make_shared<SwapchainPayload>();
    payload->desc = desc;
    const TextureDesc imageDesc{
        .extent = {desc.extent.width, desc.extent.height, 1},
        .format = desc.format,
        .usage = TextureUsage::color_attachment | TextureUsage::present,
        .debugName = desc.debugName + " image",
    };
    const auto requirements = texture_requirements(imageDesc);
    auto reservationResult = reserve_memory(
        device->memory, imageDesc.memory, requirements, "swapchain image");
    if (!reservationResult.ok()) {
        return reservationResult.status();
    }
    auto reservation = std::move(reservationResult).value();
    payload->image = std::make_shared<TexturePayload>(
        imageDesc, std::move(reservation), nullptr, true);
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.swapchainsCreated; });
    return runtime.allocate(ObjectKind::swapchain, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_upload_ring(
    Runtime& runtime, Handle deviceHandle, std::uint32_t frameCount,
    std::size_t bytesPerFrame) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "upload ring creation");
    }
    if (frameCount == 0 || bytesPerFrame == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "upload ring dimensions must be non-zero");
    }
    auto payload = std::make_shared<UploadRingPayload>();
    payload->bytesPerFrame = bytesPerFrame;
    payload->frames.reserve(frameCount);
    for (std::uint32_t index = 0; index < frameCount; ++index) {
        BufferDesc bufferDesc{
            .size = bytesPerFrame,
            .usage = BufferUsage::vertex | BufferUsage::uniform |
                     BufferUsage::storage | BufferUsage::copy_source,
            .memory = MemoryDomain::upload,
            .mappedAtCreation = true,
            .debugName = "frame upload " + std::to_string(index),
        };
        const MemoryRequirements requirements{bytesPerFrame, 16};
        auto reservationResult = reserve_memory(
            device->memory, bufferDesc.memory, requirements,
            "upload ring frame");
        if (!reservationResult.ok()) {
            return reservationResult.status();
        }
        auto reservation = std::move(reservationResult).value();
        payload->frames.push_back(std::make_shared<BufferPayload>(
            std::move(bufferDesc), std::move(reservation), nullptr, true));
    }
    return runtime.allocate(ObjectKind::upload_ring, std::move(payload));
}

[[nodiscard]] Status foundation_submit(Runtime& runtime, Handle queueHandle,
                                       std::span<const Handle> commandLists,
                                       Handle fenceHandle,
                                       std::uint64_t fenceValue) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    std::lock_guard queueLock{queue->submitMutex};
    std::vector<std::shared_ptr<CommandListPayload>> lists;
    std::vector<TransferCommand> transfers;
    lists.reserve(commandLists.size());
    for (const auto handle : commandLists) {
        auto list = runtime.resolve<CommandListPayload>(ObjectKind::command_list,
                                                        handle);
        if (!list) {
            return invalid_object("command list");
        }
        std::lock_guard listLock{list->mutex};
        if (list->state != CommandListState::executable) {
            return Status::failure(StatusCode::invalid_state,
                                   "submitted command list is not executable");
        }
        if (list->kind != queue->kind) {
            return Status::failure(StatusCode::invalid_argument,
                                   "command list queue kind does not match queue");
        }
        transfers.insert(transfers.end(), list->transfers.begin(),
                         list->transfers.end());
        lists.push_back(std::move(list));
    }
    if (runtime.config.logicalResources) {
        for (const auto& transfer : transfers) {
            if (auto status = execute_logical_transfer(transfer); !status.ok()) {
                return status;
            }
        }
    } else {
        if (runtime.config.nativeSubmit == nullptr) {
            return unsupported(runtime, "command submission");
        }
        std::vector<NativeTransfer> nativeTransfers;
        nativeTransfers.reserve(transfers.size());
        for (const auto& transfer : transfers) {
            nativeTransfers.push_back(make_native_transfer(transfer));
        }
        if (auto status = runtime.config.nativeSubmit(nativeTransfers);
            !status.ok()) {
            return status;
        }
    }
    for (const auto& list : lists) {
        std::lock_guard listLock{list->mutex};
        list->state = CommandListState::submitted;
        list->retained.clear();
        list->transfers.clear();
    }
    if (fenceHandle != 0) {
        const auto fence = runtime.resolve<FencePayload>(ObjectKind::fence,
                                                         fenceHandle);
        if (!fence) {
            return invalid_object("fence");
        }
        {
            std::lock_guard fenceLock{fence->mutex};
            fence->value = std::max(fence->value, fenceValue);
        }
        fence->changed.notify_all();
    }
    runtime.update_stats([transferCount = transfers.size()](BackendStats& stats) {
        ++stats.submissions;
        stats.transfersExecuted += transferCount;
    });
    return Status::success();
}

[[nodiscard]] Status foundation_present(Runtime& runtime, Handle queueHandle,
                                        Handle swapchainHandle,
                                        std::uint32_t imageIndex) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    const auto swapchain = runtime.resolve<SwapchainPayload>(
        ObjectKind::swapchain, swapchainHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    if (!swapchain) {
        return invalid_object("swapchain");
    }
    if (queue->kind != QueueKind::graphics) {
        return Status::failure(StatusCode::invalid_argument,
                               "presentation requires a graphics queue");
    }
    if (!runtime.config.logicalResources) {
        return unsupported(runtime, "presentation");
    }
    std::lock_guard lock{swapchain->mutex};
    if (!swapchain->acquired || imageIndex >= swapchain->desc.imageCount) {
        return Status::failure(StatusCode::invalid_state,
                               "present requires an acquired swapchain image");
    }
    swapchain->acquired = false;
    runtime.update_stats([](BackendStats& stats) { ++stats.presentations; });
    return Status::success();
}

const BackendDispatch kFoundationDispatch{
    &foundation_create_adapter,
    &foundation_create_device,
    &foundation_create_queue,
    &foundation_create_command_pool,
    &foundation_allocate_command_list,
    &foundation_create_buffer,
    &foundation_create_buffer_view,
    &foundation_create_texture,
    &foundation_create_texture_view,
    &foundation_import_buffer,
    &foundation_import_texture,
    &foundation_create_shader,
    &foundation_create_pipeline,
    &foundation_create_compute_pipeline,
    &foundation_create_fence,
    &foundation_create_semaphore,
    &foundation_create_query_pool,
    &foundation_create_surface,
    &foundation_create_swapchain,
    &foundation_create_upload_ring,
    &foundation_submit,
    &foundation_present,
};

Result<Instance> create_foundation_instance(const InstanceDesc& desc,
                                            FoundationBackendConfig config) {
    if (config.adapterName.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "foundation backend requires an adapter name");
    }
    auto runtime = std::make_shared<Runtime>();
    runtime->instanceDesc = desc;
    runtime->config = std::move(config);
    runtime->dispatch = &kFoundationDispatch;
    const auto handle = runtime->allocate(ObjectKind::instance,
                                          std::make_shared<int>(0));
    return Factory::instance(runtime, handle);
}

Result<Instance> unavailable_backend(BackendKind kind, std::string backendName) {
    (void)kind;
    return Status::failure(
        StatusCode::unsupported,
        std::move(backendName) +
            " has no native RHI 1 implementation; no simulated adapter is exposed");
}

[[nodiscard]] bool state_valid(const std::unique_ptr<ObjectState>& state) noexcept {
    return state && state->runtime &&
           state->runtime->valid(state->kind, state->handle);
}

[[nodiscard]] ObjectId state_id(const std::unique_ptr<ObjectState>& state) noexcept {
    return state_valid(state) ? ObjectId{state->handle} : ObjectId{};
}

[[nodiscard]] Status record_transfer(ObjectState& state,
                                     TransferCommand command) {
    const auto list = state.runtime->resolve<CommandListPayload>(
        ObjectKind::command_list, state.handle);
    if (!list) {
        return invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() ||
        list->state != CommandListState::recording ||
        list->activeEncoder != 3) {
        return Status::failure(
            StatusCode::invalid_state,
            "transfer command requires an owned active copy encoder");
    }
    list->transfers.push_back(std::move(command));
    return Status::success();
}

template <typename Payload>
[[nodiscard]] std::shared_ptr<Payload> payload(
    const std::unique_ptr<ObjectState>& state, ObjectKind kind) {
    if (!state || !state->runtime) {
        return {};
    }
    return state->runtime->resolve<Payload>(kind, state->handle);
}

} // namespace detail

#define TRUFFLE_DEFINE_OBJECT_LIFETIME(Type)                                      \
    Type::Type() noexcept = default;                                               \
    Type::~Type() = default;                                                       \
    Type::Type(Type&&) noexcept = default;                                         \
    Type& Type::operator=(Type&&) noexcept = default;                              \
    Type::Type(std::unique_ptr<detail::ObjectState> state) noexcept                \
        : state_(std::move(state)) {}                                              \
    bool Type::valid() const noexcept { return detail::state_valid(state_); }       \
    ObjectId Type::id() const noexcept { return detail::state_id(state_); }

TRUFFLE_DEFINE_OBJECT_LIFETIME(Instance)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Adapter)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Device)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Queue)
TRUFFLE_DEFINE_OBJECT_LIFETIME(CommandPool)
TRUFFLE_DEFINE_OBJECT_LIFETIME(CommandList)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Buffer)
TRUFFLE_DEFINE_OBJECT_LIFETIME(BufferView)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Texture)
TRUFFLE_DEFINE_OBJECT_LIFETIME(TextureView)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Shader)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Pipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(ComputePipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Fence)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Semaphore)
TRUFFLE_DEFINE_OBJECT_LIFETIME(QueryPool)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Surface)

#undef TRUFFLE_DEFINE_OBJECT_LIFETIME

BackendKind Instance::backend() const noexcept {
    return valid() ? state_->runtime->config.kind : BackendKind::null_validation;
}

std::size_t Instance::adapter_count() const noexcept {
    return valid() && !state_->runtime->config.adapterName.empty() ? 1u : 0u;
}

Result<Adapter> Instance::adapter(std::size_t index) const {
    if (!valid()) {
        return detail::invalid_object("instance");
    }
    auto handle = state_->runtime->dispatch->create_adapter(*state_->runtime, index);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::adapter(state_->runtime, handle.value());
}

BackendStats Instance::stats() const noexcept {
    return valid() ? state_->runtime->stats_snapshot() : BackendStats{};
}

const AdapterInfo& Adapter::info() const {
    const auto value = detail::payload<detail::AdapterPayload>(
        state_, detail::ObjectKind::adapter);
    if (!value) {
        static const AdapterInfo invalid{};
        return invalid;
    }
    return value->info;
}

Result<Device> Adapter::request_device(const DeviceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("adapter");
    }
    auto handle = state_->runtime->dispatch->create_device(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::device(state_->runtime, handle.value());
}

bool Device::lost() const noexcept {
    const auto value = detail::payload<detail::DevicePayload>(
        state_, detail::ObjectKind::device);
    return !value || value->lost.load();
}

const AdapterInfo& Device::adapter_info() const {
    const auto value = detail::payload<detail::DevicePayload>(
        state_, detail::ObjectKind::device);
    if (!value) {
        static const AdapterInfo invalid{};
        return invalid;
    }
    return value->adapter;
}

Result<Queue> Device::queue(QueueKind kind) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_queue(
        *state_->runtime, state_->handle, kind);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::queue(state_->runtime, handle.value());
}

Result<CommandPool> Device::create_command_pool(QueueKind kind) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_command_pool(
        *state_->runtime, state_->handle, kind);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::command_pool(state_->runtime, handle.value());
}

Result<Buffer> Device::create_buffer(const BufferDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_buffer(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::buffer(state_->runtime, handle.value());
}

Result<Texture> Device::create_texture(const TextureDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_texture(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::texture(state_->runtime, handle.value());
}

Result<BufferView> Device::create_buffer_view(Buffer& buffer,
                                              const BufferViewDesc& desc) const {
    if (!valid() || !buffer.valid() ||
        buffer.state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(
            StatusCode::invalid_argument,
            "buffer view resource must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_buffer_view(
        *state_->runtime, state_->handle, buffer.state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::buffer_view(state_->runtime, handle.value());
}

Result<TextureView> Device::create_texture_view(
    Texture& texture, const TextureViewDesc& desc) const {
    if (!valid() || !texture.valid() ||
        texture.state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(
            StatusCode::invalid_argument,
            "texture view resource must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_texture_view(
        *state_->runtime, state_->handle, texture.state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::texture_view(state_->runtime, handle.value());
}

Result<Buffer> Device::import_buffer(const BufferDesc& desc,
                                     ExternalMemoryHandle external) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->import_buffer(
        *state_->runtime, state_->handle, desc, external);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::buffer(state_->runtime, handle.value());
}

Result<Texture> Device::import_texture(const TextureDesc& desc,
                                       ExternalMemoryHandle external) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->import_texture(
        *state_->runtime, state_->handle, desc, external);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::texture(state_->runtime, handle.value());
}

Result<MemoryBudget> Device::memory_budget(MemoryDomain domain) const {
    const auto device = detail::payload<detail::DevicePayload>(
        state_, detail::ObjectKind::device);
    if (!device || !device->memory) {
        return detail::invalid_object("device");
    }
    return device->memory->budget(domain);
}

Result<Shader> Device::create_shader(const ShaderDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_shader(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::shader(state_->runtime, handle.value());
}

Result<Pipeline> Device::create_pipeline(const PipelineDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    const auto shader_belongs_to_device = [this](const Shader* shader) {
        return shader == nullptr ||
               (shader->valid() &&
                shader->state_->runtime.get() == state_->runtime.get());
    };
    if (!shader_belongs_to_device(desc.vertexShader) ||
        !shader_belongs_to_device(desc.fragmentShader)) {
        return Status::failure(StatusCode::invalid_argument,
                               "pipeline shaders must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_pipeline(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::pipeline(state_->runtime, handle.value());
}

Result<ComputePipeline> Device::create_compute_pipeline(
    const ComputePipelineDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    if (desc.computeShader != nullptr &&
        (!desc.computeShader->valid() ||
         desc.computeShader->state_->runtime.get() != state_->runtime.get())) {
        return Status::failure(
            StatusCode::invalid_argument,
            "compute pipeline shader must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_compute_pipeline(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::compute_pipeline(state_->runtime, handle.value());
}

Result<Fence> Device::create_fence(const FenceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_fence(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::fence(state_->runtime, handle.value());
}

Result<Semaphore> Device::create_semaphore(const SemaphoreDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_semaphore(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::semaphore(state_->runtime, handle.value());
}

Result<QueryPool> Device::create_query_pool(const QueryPoolDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_query_pool(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::query_pool(state_->runtime, handle.value());
}

Result<Surface> Device::create_surface(const SurfaceDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_surface(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::surface(state_->runtime, handle.value());
}

Result<Swapchain> Device::create_swapchain(Surface& surface,
                                           const SwapchainDesc& desc) const {
    if (!valid() || !surface.valid() ||
        surface.state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "surface must be valid and belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_swapchain(
        *state_->runtime, state_->handle, surface.state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::swapchain(state_->runtime, handle.value());
}

Result<UploadRing> Device::create_upload_ring(std::uint32_t frameCount,
                                              std::size_t bytesPerFrame) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_upload_ring(
        *state_->runtime, state_->handle, frameCount, bytesPerFrame);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::upload_ring(state_->runtime, handle.value());
}

BufferDesc Buffer::desc() const {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    return value ? value->desc : BufferDesc{};
}

Result<std::span<std::byte>> Buffer::map() {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers are not host mappable");
    }
    if (value->native) {
        if (state_->runtime->config.mapBuffer == nullptr) {
            return detail::unsupported(*state_->runtime, "buffer mapping");
        }
        auto mappedResult = state_->runtime->config.mapBuffer(value->native);
        if (!mappedResult.ok()) {
            return mappedResult.status();
        }
        {
            std::lock_guard lock{value->mutex};
            value->mapped = true;
        }
        return std::move(mappedResult).value();
    }
    std::lock_guard lock{value->mutex};
    value->mapped = true;
    return std::span<std::byte>{value->bytes};
}

Status Buffer::unmap() {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    {
        std::lock_guard lock{value->mutex};
        if (!value->mapped) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer is not mapped");
        }
    }
    if (value->native && state_->runtime->config.unmapBuffer != nullptr) {
        if (auto status = state_->runtime->config.unmapBuffer(value->native);
            !status.ok()) {
            return status;
        }
    }
    std::lock_guard lock{value->mutex};
    value->mapped = false;
    return Status::success();
}

bool Buffer::mapped() const noexcept {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return false;
    }
    std::lock_guard lock{value->mutex};
    return value->mapped;
}

Status Buffer::flush(std::size_t offset, std::size_t size) {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers are not host coherent");
    }
    if (size == whole_size) {
        size = offset <= value->desc.size ? value->desc.size - offset : 0;
    }
    if (!detail::buffer_range_valid(*value, offset, size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer flush range exceeds allocation");
    }
    if (value->native && state_->runtime->config.flushBuffer != nullptr) {
        return state_->runtime->config.flushBuffer(value->native, offset, size);
    }
    return Status::success();
}

Status Buffer::invalidate(std::size_t offset, std::size_t size) {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers are not host coherent");
    }
    if (size == whole_size) {
        size = offset <= value->desc.size ? value->desc.size - offset : 0;
    }
    if (!detail::buffer_range_valid(*value, offset, size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer invalidate range exceeds allocation");
    }
    if (value->native && state_->runtime->config.invalidateBuffer != nullptr) {
        return state_->runtime->config.invalidateBuffer(value->native, offset,
                                                         size);
    }
    return Status::success();
}

Status Buffer::write(std::size_t offset, std::span<const std::byte> data) {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers require transfer upload");
    }
    if (!detail::buffer_range_valid(*value, offset, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer write exceeds allocation");
    }
    if (value->native) {
        if (state_->runtime->config.writeBuffer == nullptr) {
            return detail::unsupported(*state_->runtime, "buffer write");
        }
        return state_->runtime->config.writeBuffer(value->native, offset, data);
    }
    std::lock_guard lock{value->mutex};
    std::copy(data.begin(), data.end(), value->bytes.begin() +
                                             static_cast<std::ptrdiff_t>(offset));
    return Status::success();
}

Status Buffer::read(std::size_t offset, std::span<std::byte> data) const {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local buffers require transfer readback");
    }
    if (!detail::buffer_range_valid(*value, offset, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer read exceeds allocation");
    }
    if (value->native) {
        if (state_->runtime->config.readBuffer == nullptr) {
            return detail::unsupported(*state_->runtime, "buffer read");
        }
        return state_->runtime->config.readBuffer(value->native, offset, data);
    }
    std::lock_guard lock{value->mutex};
    std::copy(value->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              value->bytes.begin() + static_cast<std::ptrdiff_t>(offset + data.size()),
              data.begin());
    return Status::success();
}

MemoryRequirements Buffer::memory_requirements() const noexcept {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    return value ? MemoryRequirements{value->desc.size, 16}
                 : MemoryRequirements{};
}

Result<ExternalMemoryHandle> Buffer::export_memory() const {
    const auto value = detail::payload<detail::BufferPayload>(
        state_, detail::ObjectKind::buffer);
    if (!value) {
        return detail::invalid_object("buffer");
    }
    return detail::unsupported(*state_->runtime, "external buffer export");
}

BufferViewDesc BufferView::desc() const {
    const auto value = detail::payload<detail::BufferViewPayload>(
        state_, detail::ObjectKind::buffer_view);
    return value ? value->desc : BufferViewDesc{};
}

ObjectId BufferView::buffer_id() const noexcept {
    const auto value = detail::payload<detail::BufferViewPayload>(
        state_, detail::ObjectKind::buffer_view);
    return value ? ObjectId{value->bufferHandle} : ObjectId{};
}

TextureDesc Texture::desc() const {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    return value ? value->desc : TextureDesc{};
}

Status Texture::write(const TextureRegion& region,
                      std::span<const std::byte> data,
                      const TextureDataLayout& layout) {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    if (!value) {
        return detail::invalid_object("texture");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local textures require transfer upload");
    }
    if (value->native) {
        if (state_->runtime->config.writeTexture == nullptr) {
            return detail::unsupported(*state_->runtime, "texture write");
        }
        return state_->runtime->config.writeTexture(value->native, region, data,
                                                     layout);
    }
    return detail::logical_texture_transfer(*value, region, {}, data, layout,
                                            true);
}

Status Texture::read(const TextureRegion& region, std::span<std::byte> data,
                     const TextureDataLayout& layout) const {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    if (!value) {
        return detail::invalid_object("texture");
    }
    if (value->desc.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local textures require transfer readback");
    }
    if (value->native) {
        if (state_->runtime->config.readTexture == nullptr) {
            return detail::unsupported(*state_->runtime, "texture read");
        }
        return state_->runtime->config.readTexture(value->native, region, data,
                                                    layout);
    }
    return detail::logical_texture_transfer(*value, region, data, {}, layout,
                                            false);
}

MemoryRequirements Texture::memory_requirements() const noexcept {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    return value ? detail::texture_requirements(value->desc)
                 : MemoryRequirements{};
}

Result<ExternalMemoryHandle> Texture::export_memory() const {
    const auto value = detail::payload<detail::TexturePayload>(
        state_, detail::ObjectKind::texture);
    if (!value) {
        return detail::invalid_object("texture");
    }
    return detail::unsupported(*state_->runtime, "external texture export");
}

TextureViewDesc TextureView::desc() const {
    const auto value = detail::payload<detail::TextureViewPayload>(
        state_, detail::ObjectKind::texture_view);
    return value ? value->desc : TextureViewDesc{};
}

ObjectId TextureView::texture_id() const noexcept {
    const auto value = detail::payload<detail::TextureViewPayload>(
        state_, detail::ObjectKind::texture_view);
    return value ? ObjectId{value->textureHandle} : ObjectId{};
}

ShaderStage Shader::stage() const {
    const auto value = detail::payload<detail::ShaderPayload>(
        state_, detail::ObjectKind::shader);
    return value ? value->desc.stage : ShaderStage::vertex;
}

const PipelineReflection& Shader::reflection() const {
    const auto value = detail::payload<detail::ShaderPayload>(
        state_, detail::ObjectKind::shader);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

const PipelineReflection& Pipeline::reflection() const {
    const auto value = detail::payload<detail::PipelinePayload>(
        state_, detail::ObjectKind::pipeline);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

const PipelineReflection& ComputePipeline::reflection() const {
    const auto value = detail::payload<detail::ComputePipelinePayload>(
        state_, detail::ObjectKind::compute_pipeline);
    if (!value) {
        static const PipelineReflection empty;
        return empty;
    }
    return value->reflection;
}

Extent3D ComputePipeline::preferred_workgroup_size() const {
    const auto value = detail::payload<detail::ComputePipelinePayload>(
        state_, detail::ObjectKind::compute_pipeline);
    return value ? value->preferredWorkgroupSize : Extent3D{};
}

Result<CommandList> CommandPool::allocate() const {
    if (!valid()) {
        return detail::invalid_object("command pool");
    }
    auto handle = state_->runtime->dispatch->allocate_command_list(
        *state_->runtime, state_->handle);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::command_list(state_->runtime, handle.value());
}

Status CommandPool::reset() {
    const auto value = detail::payload<detail::CommandPoolPayload>(
        state_, detail::ObjectKind::command_pool);
    if (!value) {
        return detail::invalid_object("command pool");
    }
    if (value->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command pool is owned by another thread");
    }
    return Status::success();
}

CommandListState CommandList::state() const noexcept {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return CommandListState::invalid;
    }
    std::lock_guard lock{value->mutex};
    return value->state;
}

Status CommandList::begin() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->state != CommandListState::initial) {
        return Status::failure(StatusCode::invalid_state,
                               "command list begin requires initial state");
    }
    value->owner = std::this_thread::get_id();
    value->state = CommandListState::recording;
    value->graphicsPipelineBound = false;
    value->computePipelineBound = false;
    return Status::success();
}

Status CommandList::end() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "command list is owned by another thread");
    }
    if (value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "command list end requires recording with no active encoder");
    }
    value->state = CommandListState::executable;
    return Status::success();
}

Status CommandList::reset() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->state == CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "recording command list cannot be reset");
    }
    value->state = CommandListState::initial;
    value->owner = {};
    value->retained.clear();
    value->transfers.clear();
    value->graphicsPipelineBound = false;
    value->computePipelineBound = false;
    return Status::success();
}

Result<RenderEncoder> CommandList::begin_rendering(const RenderPassDesc& desc) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "render encoder requires owned recording command list");
    }
    if (!validation::is_non_zero(desc.extent)) {
        return Status::failure(StatusCode::invalid_argument,
                               "render extent must be non-zero");
    }
    std::vector<std::shared_ptr<void>> attachments;
    attachments.reserve(desc.colorAttachments.size());
    for (const auto& attachment : desc.colorAttachments) {
        if (attachment.texture == nullptr) {
            continue;
        }
        if (!attachment.texture->valid() ||
            attachment.texture->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "render attachments must belong to the command-list runtime");
        }
        auto retained = state_->runtime->retain(
            detail::ObjectKind::texture, attachment.texture->state_->handle);
        if (!retained) {
            return detail::invalid_object("render attachment");
        }
        attachments.push_back(std::move(retained));
    }
    value->retained.insert(value->retained.end(), attachments.begin(),
                           attachments.end());
    value->activeEncoder = 1;
    value->graphicsPipelineBound = false;
    return RenderEncoder{*this};
}

Result<ComputeEncoder> CommandList::begin_compute() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "compute encoder requires owned recording command list");
    }
    value->activeEncoder = 2;
    value->computePipelineBound = false;
    return ComputeEncoder{*this};
}

Result<CopyEncoder> CommandList::begin_copy() {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "copy encoder requires owned recording command list");
    }
    if (value->kind == QueueKind::compute) {
        return Status::failure(StatusCode::unsupported,
                               "compute command lists cannot encode transfers");
    }
    value->activeEncoder = 3;
    return CopyEncoder{*this};
}

Status CommandList::encoder_command(std::uint32_t opcode, ObjectId object,
                                    std::uint64_t arg0, std::uint64_t arg1) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder == 0) {
        return Status::failure(StatusCode::invalid_state,
                               "encoder command requires active owned recording");
    }
    switch (opcode) {
    case 1: {
        auto retained = state_->runtime->retain(detail::ObjectKind::pipeline,
                                                object.value);
        if (!retained || value->activeEncoder != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics pipeline is invalid for encoder");
        }
        value->retained.push_back(std::move(retained));
        value->graphicsPipelineBound = true;
        break;
    }
    case 2:
    case 3:
    case 5:
    case 7:
    case 8: {
        auto retained = state_->runtime->retain(detail::ObjectKind::buffer,
                                                object.value);
        if (!retained) {
            return detail::invalid_object("buffer");
        }
        value->retained.push_back(std::move(retained));
        break;
    }
    case 4: {
        auto retained = state_->runtime->retain(
            detail::ObjectKind::compute_pipeline, object.value);
        if (!retained || value->activeEncoder != 2) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline is invalid for encoder");
        }
        value->retained.push_back(std::move(retained));
        value->computePipelineBound = true;
        break;
    }
    case 6:
        if (value->activeEncoder != 1 || !value->graphicsPipelineBound ||
            arg0 == 0 || arg1 == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "draw requires a pipeline and non-zero counts");
        }
        state_->runtime->update_stats(
            [](BackendStats& stats) { ++stats.drawsRecorded; });
        break;
    case 9:
        if (value->activeEncoder != 2 || !value->computePipelineBound ||
            arg0 == 0 || arg1 == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch requires a pipeline and non-zero groups");
        }
        state_->runtime->update_stats(
            [](BackendStats& stats) { ++stats.dispatchesRecorded; });
        break;
    default:
        return Status::failure(StatusCode::invalid_argument,
                               "unknown encoder command");
    }
    return Status::success();
}

Status CommandList::end_encoder(std::uint32_t encoderKind) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->activeEncoder != encoderKind) {
        return Status::failure(StatusCode::invalid_state,
                               "encoder end does not match active encoder");
    }
    value->activeEncoder = 0;
    return Status::success();
}

void CommandList::abandon_encoder(std::uint32_t encoderKind) noexcept {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return;
    }
    std::lock_guard lock{value->mutex};
    if (value->activeEncoder == encoderKind) {
        value->activeEncoder = 0;
        value->state = CommandListState::invalid;
    }
}

RenderEncoder::RenderEncoder() noexcept = default;
RenderEncoder::RenderEncoder(CommandList& list) noexcept
    : list_(&list), active_(true) {}
RenderEncoder::~RenderEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(1);
    }
}
RenderEncoder::RenderEncoder(RenderEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
RenderEncoder& RenderEncoder::operator=(RenderEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(1);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status RenderEncoder::bind_pipeline(Pipeline& pipeline) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!pipeline.valid() ||
        pipeline.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics pipeline belongs to another runtime");
    }
    return list_->encoder_command(1, pipeline.id());
}

Status RenderEncoder::bind_vertex_buffer(std::uint32_t slot, Buffer& buffer,
                                         std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "vertex buffer belongs to another runtime");
    }
    return list_->encoder_command(2, buffer.id(), slot, offset);
}

Status RenderEncoder::bind_uniform_buffer(std::uint32_t slot, Buffer& buffer,
                                          std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "uniform buffer belongs to another runtime");
    }
    return list_->encoder_command(3, buffer.id(), slot, offset);
}

Status RenderEncoder::bind_index_buffer(Buffer& buffer, std::size_t offset,
                                        IndexFormat format) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "index buffer belongs to another runtime");
    }
    return list_->encoder_command(5, buffer.id(), offset,
                                  static_cast<std::uint64_t>(format));
}

Status RenderEncoder::draw(std::uint32_t vertexCount,
                           std::uint32_t instanceCount,
                           std::uint32_t firstVertex,
                           std::uint32_t firstInstance) {
    (void)firstVertex;
    (void)firstInstance;
    return active_ && list_
               ? list_->encoder_command(6, {}, vertexCount, instanceCount)
               : detail::invalid_object("render encoder");
}

Status RenderEncoder::draw_indexed(std::uint32_t indexCount,
                                   std::uint32_t instanceCount,
                                   std::uint32_t firstIndex,
                                   std::int32_t vertexOffset,
                                   std::uint32_t firstInstance) {
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
    return draw(indexCount, instanceCount);
}

Status RenderEncoder::draw_indirect(Buffer& buffer, std::size_t offset,
                                    bool indexed) {
    if (auto status = bind_index_buffer(buffer, offset,
                                        indexed ? IndexFormat::uint32
                                                : IndexFormat::uint16);
        !status.ok()) {
        return status;
    }
    return draw(1, 1);
}

Status RenderEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    auto status = list_->end_encoder(1);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

ComputeEncoder::ComputeEncoder() noexcept = default;
ComputeEncoder::ComputeEncoder(CommandList& list) noexcept
    : list_(&list), active_(true) {}
ComputeEncoder::~ComputeEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(2);
    }
}
ComputeEncoder::ComputeEncoder(ComputeEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
ComputeEncoder& ComputeEncoder::operator=(ComputeEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(2);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status ComputeEncoder::bind_pipeline(ComputePipeline& pipeline) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    if (!pipeline.valid() ||
        pipeline.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute pipeline belongs to another runtime");
    }
    return list_->encoder_command(4, pipeline.id());
}

Status ComputeEncoder::bind_storage_buffer(std::uint32_t slot, Buffer& buffer,
                                           std::size_t offset) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    if (!buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "storage buffer belongs to another runtime");
    }
    return list_->encoder_command(5, buffer.id(), slot, offset);
}

Status ComputeEncoder::dispatch(std::uint32_t x, std::uint32_t y,
                                std::uint32_t z) {
    if (z == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "dispatch group counts must be non-zero");
    }
    return active_ && list_
               ? list_->encoder_command(9, {}, x, y)
               : detail::invalid_object("compute encoder");
}

Status ComputeEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    auto status = list_->end_encoder(2);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

CopyEncoder::CopyEncoder() noexcept = default;
CopyEncoder::CopyEncoder(CommandList& list) noexcept : list_(&list), active_(true) {}
CopyEncoder::~CopyEncoder() {
    if (active_ && list_ != nullptr) {
        list_->abandon_encoder(3);
    }
}
CopyEncoder::CopyEncoder(CopyEncoder&& other) noexcept
    : list_(std::exchange(other.list_, nullptr)),
      active_(std::exchange(other.active_, false)) {}
CopyEncoder& CopyEncoder::operator=(CopyEncoder&& other) noexcept {
    if (this != &other) {
        if (active_ && list_ != nullptr) {
            list_->abandon_encoder(3);
        }
        list_ = std::exchange(other.list_, nullptr);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

Status CopyEncoder::copy_buffer(Buffer& source, std::size_t sourceOffset,
                                Buffer& destination,
                                std::size_t destinationOffset,
                                std::size_t size) {
    if (!active_ || list_ == nullptr || size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "copy requires an active encoder and non-zero size");
    }
    if (!source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "copy buffers must belong to the command-list runtime");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.bufferCopy) {
        return detail::unsupported(*list_->state_->runtime, "buffer copy");
    }
    const auto sourcePayload = detail::payload<detail::BufferPayload>(
        source.state_, detail::ObjectKind::buffer);
    const auto destinationPayload = detail::payload<detail::BufferPayload>(
        destination.state_, detail::ObjectKind::buffer);
    if (!sourcePayload || !destinationPayload ||
        !has_usage(sourcePayload->desc.usage, BufferUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   BufferUsage::copy_destination) ||
        !detail::buffer_range_valid(*sourcePayload, sourceOffset, size) ||
        !detail::buffer_range_valid(*destinationPayload, destinationOffset,
                                    size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer copy usage or range is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::copy_buffer;
    command.sourceBuffer = sourcePayload;
    command.destinationBuffer = destinationPayload;
    command.buffer = {sourceOffset, destinationOffset, size};
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::copy_buffer(Buffer& source, Buffer& destination,
                                const BufferCopyRegion& region) {
    return copy_buffer(source, region.sourceOffset, destination,
                       region.destinationOffset, region.size);
}

Status CopyEncoder::fill_buffer(Buffer& destination, std::size_t offset,
                                std::size_t size, std::byte value) {
    if (!active_ || list_ == nullptr || size == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "fill requires an active encoder and non-zero size");
    }
    if (!destination.valid() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "fill buffer belongs to another runtime");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.bufferFill) {
        return detail::unsupported(*list_->state_->runtime, "buffer fill");
    }
    const auto destinationPayload = detail::payload<detail::BufferPayload>(
        destination.state_, detail::ObjectKind::buffer);
    if (!destinationPayload ||
        !has_usage(destinationPayload->desc.usage,
                   BufferUsage::copy_destination) ||
        !detail::buffer_range_valid(*destinationPayload, offset, size)) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer fill usage or range is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::fill_buffer;
    command.destinationBuffer = destinationPayload;
    command.buffer = {.destinationOffset = offset, .size = size};
    command.fillValue = value;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::copy_buffer_to_texture(
    Buffer& source, Texture& destination,
    const BufferTextureCopyRegion& region) {
    if (!active_ || list_ == nullptr || !source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "buffer-to-texture objects are invalid or foreign");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.bufferTextureCopy) {
        return detail::unsupported(*list_->state_->runtime,
                                   "buffer-to-texture copy");
    }
    const auto sourcePayload = detail::payload<detail::BufferPayload>(
        source.state_, detail::ObjectKind::buffer);
    const auto destinationPayload = detail::payload<detail::TexturePayload>(
        destination.state_, detail::ObjectKind::texture);
    auto layout = detail::buffer_texture_layout(region);
    const auto required = destinationPayload
                              ? detail::texture_data_size(
                                    destinationPayload->desc, region.texture, layout)
                              : 0;
    if (!sourcePayload || !destinationPayload || required == 0 ||
        required > sourcePayload->desc.size ||
        !has_usage(sourcePayload->desc.usage, BufferUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   TextureUsage::copy_destination)) {
        return Status::failure(
            StatusCode::invalid_argument,
            "buffer-to-texture usage, region, or layout is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::copy_buffer_to_texture;
    command.sourceBuffer = sourcePayload;
    command.destinationTexture = destinationPayload;
    command.bufferTexture = region;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::copy_texture_to_buffer(
    Texture& source, Buffer& destination,
    const BufferTextureCopyRegion& region) {
    if (!active_ || list_ == nullptr || !source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture-to-buffer objects are invalid or foreign");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.bufferTextureCopy) {
        return detail::unsupported(*list_->state_->runtime,
                                   "texture-to-buffer copy");
    }
    const auto sourcePayload = detail::payload<detail::TexturePayload>(
        source.state_, detail::ObjectKind::texture);
    const auto destinationPayload = detail::payload<detail::BufferPayload>(
        destination.state_, detail::ObjectKind::buffer);
    auto layout = detail::buffer_texture_layout(region);
    const auto required = sourcePayload
                              ? detail::texture_data_size(
                                    sourcePayload->desc, region.texture, layout)
                              : 0;
    if (!sourcePayload || !destinationPayload || required == 0 ||
        required > destinationPayload->desc.size ||
        !has_usage(sourcePayload->desc.usage, TextureUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   BufferUsage::copy_destination)) {
        return Status::failure(
            StatusCode::invalid_argument,
            "texture-to-buffer usage, region, or layout is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::copy_texture_to_buffer;
    command.sourceTexture = sourcePayload;
    command.destinationBuffer = destinationPayload;
    command.bufferTexture = region;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::copy_texture(Texture& source, Texture& destination,
                                 const TextureCopyRegion& region) {
    if (!active_ || list_ == nullptr || !source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture copy objects are invalid or foreign");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.textureCopy) {
        return detail::unsupported(*list_->state_->runtime, "texture copy");
    }
    const auto sourcePayload = detail::payload<detail::TexturePayload>(
        source.state_, detail::ObjectKind::texture);
    const auto destinationPayload = detail::payload<detail::TexturePayload>(
        destination.state_, detail::ObjectKind::texture);
    if (!sourcePayload || !destinationPayload ||
        !detail::texture_region_valid(sourcePayload->desc, region.source) ||
        !detail::texture_region_valid(destinationPayload->desc,
                                      region.destination) ||
        sourcePayload->desc.format != destinationPayload->desc.format ||
        sourcePayload->desc.sampleCount != destinationPayload->desc.sampleCount ||
        region.source.extent.width != region.destination.extent.width ||
        region.source.extent.height != region.destination.extent.height ||
        region.source.extent.depth != region.destination.extent.depth ||
        !has_usage(sourcePayload->desc.usage, TextureUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   TextureUsage::copy_destination)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture copy usage or regions are incompatible");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::copy_texture;
    command.sourceTexture = sourcePayload;
    command.destinationTexture = destinationPayload;
    command.texture = region;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::clear_texture(Texture& texture, const TextureRegion& region,
                                  const ClearValue& value) {
    if (!active_ || list_ == nullptr || !texture.valid() ||
        texture.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "clear texture is invalid or foreign");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.textureClear) {
        return detail::unsupported(*list_->state_->runtime, "texture clear");
    }
    const auto payload = detail::payload<detail::TexturePayload>(
        texture.state_, detail::ObjectKind::texture);
    const auto info = payload ? detail::format_info(payload->desc.format)
                              : detail::FormatInfo{};
    if (!payload || !detail::texture_region_valid(payload->desc, region) ||
        info.compressed ||
        (!has_usage(payload->desc.usage, TextureUsage::copy_destination) &&
         !has_usage(payload->desc.usage, TextureUsage::color_attachment) &&
         !has_usage(payload->desc.usage,
                    TextureUsage::depth_stencil_attachment))) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture clear usage, format, or region is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::clear_texture;
    command.destinationTexture = payload;
    command.texture.destination = region;
    command.clear = value;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::resolve_texture(Texture& source, Texture& destination,
                                    const TextureCopyRegion& region) {
    if (!active_ || list_ == nullptr || !source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "resolve textures are invalid or foreign");
    }
    if (!list_->state_->runtime->config.resourceCapabilities.textureResolve) {
        return detail::unsupported(*list_->state_->runtime, "texture resolve");
    }
    const auto sourcePayload = detail::payload<detail::TexturePayload>(
        source.state_, detail::ObjectKind::texture);
    const auto destinationPayload = detail::payload<detail::TexturePayload>(
        destination.state_, detail::ObjectKind::texture);
    if (!sourcePayload || !destinationPayload ||
        sourcePayload->desc.sampleCount <= 1 ||
        destinationPayload->desc.sampleCount != 1 ||
        sourcePayload->desc.format != destinationPayload->desc.format ||
        !detail::texture_region_valid(sourcePayload->desc, region.source) ||
        !detail::texture_region_valid(destinationPayload->desc,
                                      region.destination) ||
        region.source.extent.width != region.destination.extent.width ||
        region.source.extent.height != region.destination.extent.height ||
        region.source.extent.depth != region.destination.extent.depth ||
        !has_usage(sourcePayload->desc.usage, TextureUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   TextureUsage::copy_destination)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture resolve contract is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::resolve_texture;
    command.sourceTexture = sourcePayload;
    command.destinationTexture = destinationPayload;
    command.texture = region;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::blit_texture(Texture& source, Texture& destination,
                                 const TextureBlitRegion& region) {
    if (!active_ || list_ == nullptr || !source.valid() || !destination.valid() ||
        source.state_->runtime.get() != list_->state_->runtime.get() ||
        destination.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "blit textures are invalid or foreign");
    }
    const auto& capabilities =
        list_->state_->runtime->config.resourceCapabilities;
    if ((region.filter == Filter::nearest && !capabilities.textureBlitNearest) ||
        (region.filter == Filter::linear && !capabilities.textureBlitLinear)) {
        return detail::unsupported(*list_->state_->runtime, "texture blit");
    }
    const auto sourcePayload = detail::payload<detail::TexturePayload>(
        source.state_, detail::ObjectKind::texture);
    const auto destinationPayload = detail::payload<detail::TexturePayload>(
        destination.state_, detail::ObjectKind::texture);
    if (!sourcePayload || !destinationPayload ||
        sourcePayload->desc.format != destinationPayload->desc.format ||
        !detail::texture_region_valid(sourcePayload->desc, region.source) ||
        !detail::texture_region_valid(destinationPayload->desc,
                                      region.destination) ||
        !has_usage(sourcePayload->desc.usage, TextureUsage::copy_source) ||
        !has_usage(destinationPayload->desc.usage,
                   TextureUsage::copy_destination)) {
        return Status::failure(StatusCode::invalid_argument,
                               "texture blit usage, format, or region is invalid");
    }
    detail::TransferCommand command;
    command.kind = detail::TransferKind::blit_texture;
    command.sourceTexture = sourcePayload;
    command.destinationTexture = destinationPayload;
    command.blit = region;
    return detail::record_transfer(*list_->state_, std::move(command));
}

Status CopyEncoder::end() {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("copy encoder");
    }
    auto status = list_->end_encoder(3);
    if (status.ok()) {
        active_ = false;
        list_ = nullptr;
    }
    return status;
}

QueueKind Queue::kind() const {
    const auto value = detail::payload<detail::QueuePayload>(
        state_, detail::ObjectKind::queue);
    return value ? value->kind : QueueKind::graphics;
}

Status Queue::submit(std::span<CommandList* const> commandLists,
                     Fence* signalFence, std::uint64_t signalValue) {
    if (!valid() || commandLists.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "submit requires a valid queue and command lists");
    }
    std::vector<detail::Handle> handles;
    handles.reserve(commandLists.size());
    for (auto* list : commandLists) {
        if (list == nullptr || !list->valid() ||
            list->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submitted command list is invalid or foreign");
        }
        handles.push_back(list->state_->handle);
    }
    detail::Handle fenceHandle = 0;
    if (signalFence != nullptr) {
        if (!signalFence->valid() ||
            signalFence->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "signal fence is invalid or foreign");
        }
        fenceHandle = signalFence->state_->handle;
    }
    return state_->runtime->dispatch->submit(*state_->runtime, state_->handle,
                                             handles, fenceHandle, signalValue);
}

Status Queue::present(Swapchain& swapchain, std::uint32_t imageIndex) {
    if (!valid() || !swapchain.valid() ||
        swapchain.state_->object->runtime.get() != state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "present objects are invalid or foreign");
    }
    return state_->runtime->dispatch->present(
        *state_->runtime, state_->handle, swapchain.state_->object->handle,
        imageIndex);
}

std::uint64_t Fence::completed_value() const noexcept {
    const auto value = detail::payload<detail::FencePayload>(
        state_, detail::ObjectKind::fence);
    if (!value) {
        return 0;
    }
    std::lock_guard lock{value->mutex};
    return value->value;
}

Status Fence::wait(std::uint64_t target, std::chrono::nanoseconds timeout) {
    const auto value = detail::payload<detail::FencePayload>(
        state_, detail::ObjectKind::fence);
    if (!value) {
        return detail::invalid_object("fence");
    }
    std::unique_lock lock{value->mutex};
    if (value->changed.wait_for(lock, timeout,
                                [&] { return value->value >= target; })) {
        return Status::success();
    }
    return Status::failure(StatusCode::timeout, "fence wait timed out");
}

std::uint64_t Semaphore::value() const noexcept {
    const auto payloadValue = detail::payload<detail::SemaphorePayload>(
        state_, detail::ObjectKind::semaphore);
    return payloadValue ? payloadValue->value.load() : 0;
}

QueryPoolDesc QueryPool::desc() const {
    const auto value = detail::payload<detail::QueryPoolPayload>(
        state_, detail::ObjectKind::query_pool);
    return value ? value->desc : QueryPoolDesc{};
}

SurfaceDesc Surface::desc() const {
    const auto value = detail::payload<detail::SurfacePayload>(
        state_, detail::ObjectKind::surface);
    return value ? value->desc : SurfaceDesc{};
}

Swapchain::Swapchain() noexcept = default;
Swapchain::~Swapchain() = default;
Swapchain::Swapchain(Swapchain&&) noexcept = default;
Swapchain& Swapchain::operator=(Swapchain&&) noexcept = default;
Swapchain::Swapchain(std::unique_ptr<detail::SwapchainState> state) noexcept
    : state_(std::move(state)) {}

bool Swapchain::valid() const noexcept {
    return state_ && detail::state_valid(state_->object);
}

ObjectId Swapchain::id() const noexcept {
    return state_ ? detail::state_id(state_->object) : ObjectId{};
}

SwapchainDesc Swapchain::desc() const {
    if (!valid()) {
        return {};
    }
    const auto value = state_->object->runtime->resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    return value ? value->desc : SwapchainDesc{};
}

AcquireResult Swapchain::acquire_next_image() {
    if (!valid()) {
        return {.status = detail::invalid_object("swapchain")};
    }
    auto& runtime = *state_->object->runtime;
    if (!runtime.config.logicalResources) {
        return {.status = detail::unsupported(runtime, "swapchain acquisition")};
    }
    const auto value = runtime.resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    if (!value) {
        return {.status = detail::invalid_object("swapchain")};
    }
    std::lock_guard lock{value->mutex};
    if (value->acquired) {
        return {.status = Status::failure(
                    StatusCode::invalid_state,
                    "previous swapchain image has not been presented")};
    }
    const auto imageHandle = runtime.allocate(detail::ObjectKind::texture,
                                              value->image);
    state_->image = std::make_unique<Texture>(
        detail::Factory::texture(state_->object->runtime, imageHandle));
    const auto semaphoreHandle = runtime.allocate(
        detail::ObjectKind::semaphore,
        std::make_shared<detail::SemaphorePayload>(1));
    state_->available = std::make_unique<Semaphore>(
        detail::Factory::semaphore(state_->object->runtime, semaphoreHandle));
    const auto index = value->nextImage++ % value->desc.imageCount;
    value->acquired = true;
    return {
        .image = state_->image.get(),
        .imageIndex = index,
        .status = Status::success(),
        .available = state_->available.get(),
        .availableValue = 1,
    };
}

Status Swapchain::resize(Extent2D extent) {
    if (!valid()) {
        return detail::invalid_object("swapchain");
    }
    if (!validation::is_non_zero(extent)) {
        return Status::failure(StatusCode::invalid_argument,
                               "swapchain extent must be non-zero");
    }
    const auto value = state_->object->runtime->resolve<detail::SwapchainPayload>(
        detail::ObjectKind::swapchain, state_->object->handle);
    if (!value) {
        return detail::invalid_object("swapchain");
    }
    std::lock_guard lock{value->mutex};
    if (value->acquired) {
        return Status::failure(StatusCode::invalid_state,
                               "cannot resize with an acquired image");
    }
    const TextureDesc imageDesc{
        .extent = {extent.width, extent.height, 1},
        .format = value->desc.format,
        .usage = TextureUsage::color_attachment | TextureUsage::present,
        .debugName = value->desc.debugName + " image",
    };
    const auto requirements = detail::texture_requirements(imageDesc);
    const auto ledger = value->image && value->image->reservation
                            ? value->image->reservation->ledger
                            : nullptr;
    auto reservationResult = detail::reserve_memory(
        ledger, imageDesc.memory, requirements, "resized swapchain image");
    if (!reservationResult.ok()) {
        return reservationResult.status();
    }
    auto reservation = std::move(reservationResult).value();
    value->image = std::make_shared<detail::TexturePayload>(
        imageDesc, std::move(reservation), nullptr, true);
    value->desc.extent = extent;
    state_->image.reset();
    state_->available.reset();
    return Status::success();
}

UploadRing::UploadRing() noexcept = default;
UploadRing::~UploadRing() = default;
UploadRing::UploadRing(UploadRing&&) noexcept = default;
UploadRing& UploadRing::operator=(UploadRing&&) noexcept = default;
UploadRing::UploadRing(std::unique_ptr<detail::UploadRingState> state) noexcept
    : state_(std::move(state)) {}

bool UploadRing::valid() const noexcept {
    return state_ && detail::state_valid(state_->object);
}

ObjectId UploadRing::id() const noexcept {
    return state_ ? detail::state_id(state_->object) : ObjectId{};
}

FrameAllocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    if (!valid() || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return {};
    }
    const auto value = state_->object->runtime->resolve<detail::UploadRingPayload>(
        detail::ObjectKind::upload_ring, state_->object->handle);
    if (!value) {
        return {};
    }
    std::lock_guard lock{value->mutex};
    const auto aligned = (value->offset + alignment - 1) & ~(alignment - 1);
    if (aligned > value->bytesPerFrame || size > value->bytesPerFrame - aligned) {
        return {};
    }
    auto& frame = value->frames[value->currentFrame];
    value->offset = aligned + size;
    return {
        .buffer = &state_->buffers[value->currentFrame],
        .offset = aligned,
        .size = size,
        .mapped = frame->bytes.data() + static_cast<std::ptrdiff_t>(aligned),
    };
}

Status UploadRing::advance() {
    if (!valid()) {
        return detail::invalid_object("upload ring");
    }
    const auto value = state_->object->runtime->resolve<detail::UploadRingPayload>(
        detail::ObjectKind::upload_ring, state_->object->handle);
    if (!value) {
        return detail::invalid_object("upload ring");
    }
    std::lock_guard lock{value->mutex};
    value->currentFrame = (value->currentFrame + 1u) % value->frames.size();
    value->offset = 0;
    return Status::success();
}

} // namespace truffle::rhi

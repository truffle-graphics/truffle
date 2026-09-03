#include "truffle/rhi/rhi.hpp"

#include "foundation_backend.hpp"
#include "truffle/rhi/validation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
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
    sampler,
    bind_group_layout,
    descriptor_arena,
    bind_group,
    bindless_table,
    pipeline_layout,
    pipeline_cache,
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

struct SemaphorePointHandle {
    Handle semaphore = 0;
    std::uint64_t value = 0;
    PipelineStage stages = PipelineStage::all_commands;
};

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
    Result<Handle> (*create_sampler)(Runtime&, Handle, const SamplerDesc&);
    Result<Handle> (*create_bind_group_layout)(Runtime&, Handle,
                                               const BindGroupLayoutDesc&);
    Result<Handle> (*create_descriptor_arena)(Runtime&, Handle,
                                              const DescriptorArenaDesc&);
    Result<Handle> (*create_bind_group)(Runtime&, Handle,
                                        const BindGroupDesc&);
    Result<Handle> (*create_bindless_table)(Runtime&, Handle,
                                            const BindlessTableDesc&);
    Result<Handle> (*create_pipeline_layout)(Runtime&, Handle,
                                             const PipelineLayoutDesc&);
    Result<Handle> (*create_pipeline_cache)(Runtime&, Handle,
                                            const PipelineCacheDesc&);
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
    Status (*submit)(Runtime&, Handle, std::span<const Handle>,
                     std::span<const SemaphorePointHandle>,
                     std::span<const SemaphorePointHandle>, Handle,
                     std::uint64_t, std::chrono::nanoseconds);
    Status (*present)(Runtime&, Handle, Handle, std::uint32_t,
                      std::span<const SemaphorePointHandle>);
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
    std::shared_ptr<DevicePayload> device;
    std::mutex submitMutex;
};

struct CommandPoolPayload {
    QueueKind kind = QueueKind::graphics;
    std::thread::id owner;
};

struct ResourceSyncState {
    PipelineStage stages = PipelineStage::top;
    Access access = Access::none;
    TextureLayout layout = TextureLayout::undefined;
    QueueKind owner = QueueKind::graphics;
    bool ownerSet = false;
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
    ResourceSyncState sync;
    mutable std::mutex mutex;
};

struct TexturePayload {
    TexturePayload(TextureDesc value,
                   std::shared_ptr<MemoryReservation> reservationValue,
                   std::shared_ptr<void> nativeValue, bool logical)
        : desc(std::move(value)), reservation(std::move(reservationValue)),
          native(std::move(nativeValue)),
          bytes(logical ? texture_requirements(desc).size : 0),
          sync(static_cast<std::size_t>(desc.mipLevels) * desc.arrayLayers) {}
    TextureDesc desc;
    std::shared_ptr<MemoryReservation> reservation;
    std::shared_ptr<void> native;
    std::vector<std::byte> bytes;
    std::vector<ResourceSyncState> sync;
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

struct SamplerPayload {
    SamplerDesc desc;
    std::shared_ptr<void> native;
};

struct BindGroupLayoutPayload {
    BindGroupLayoutDesc desc;
    std::vector<std::pair<std::uint32_t, std::shared_ptr<SamplerPayload>>>
        immutableSamplers;
};

struct DescriptorArenaPayload {
    DescriptorArenaDesc desc;
    std::thread::id owner;
    std::uint64_t epoch = 1;
    std::uint32_t allocatedBindGroups = 0;
    std::uint32_t allocatedDescriptors = 0;
    mutable std::mutex mutex;
};

struct BoundResource {
    BindGroupEntry entry;
    BindingType type = BindingType::uniform_buffer;
    std::shared_ptr<BufferPayload> buffer;
    std::shared_ptr<TextureViewPayload> textureView;
    std::shared_ptr<SamplerPayload> sampler;
};

struct BindGroupPayload {
    Handle layoutHandle = 0;
    std::shared_ptr<BindGroupLayoutPayload> layout;
    std::shared_ptr<DescriptorArenaPayload> arena;
    std::uint64_t arenaEpoch = 0;
    std::vector<BoundResource> resources;
};

struct PipelineLayoutPayload {
    PipelineLayoutDesc desc;
    std::vector<Handle> layoutHandles;
    std::vector<std::shared_ptr<BindGroupLayoutPayload>> layouts;
};

struct PipelineCachePayload {
    std::vector<std::byte> data;
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

struct PipelinePayload;
struct ComputePipelinePayload;

struct CommandListPayload {
    QueueKind kind = QueueKind::graphics;
    CommandListState state = CommandListState::initial;
    std::thread::id owner;
    std::uint32_t activeEncoder = 0;
    bool graphicsPipelineBound = false;
    bool computePipelineBound = false;
    bool indexBufferBound = false;
    bool viewportSet = true;
    bool scissorSet = true;
    bool blendConstantSet = true;
    bool stencilReferenceSet = true;
    bool depthBiasSet = true;
    std::shared_ptr<PipelinePayload> graphicsPipeline;
    std::shared_ptr<ComputePipelinePayload> computePipeline;
    std::vector<TextureFormat> renderColorFormats;
    TextureFormat renderDepthStencilFormat = TextureFormat::unknown;
    std::uint32_t renderSampleCount = 1;
    std::vector<std::shared_ptr<void>> retained;
    struct BufferBarrierCommand {
        BufferBarrier desc;
        std::shared_ptr<BufferPayload> resource;
    };
    struct TextureBarrierCommand {
        TextureBarrier desc;
        std::shared_ptr<TexturePayload> resource;
    };
    struct AliasingBarrierCommand {
        AliasingBarrier desc;
        std::shared_ptr<BufferPayload> beforeBuffer;
        std::shared_ptr<TexturePayload> beforeTexture;
        std::shared_ptr<BufferPayload> afterBuffer;
        std::shared_ptr<TexturePayload> afterTexture;
    };
    struct BarrierCommand {
        std::vector<BufferBarrierCommand> buffers;
        std::vector<TextureBarrierCommand> textures;
        std::vector<AliasingBarrierCommand> aliasing;
    };
    enum class OperationKind { command, transfer, barrier };
    struct Operation {
        OperationKind kind = OperationKind::command;
        NativeCommand native;
        TransferCommand transfer;
        BarrierCommand barrier;
    };
    std::vector<Operation> operations;
    std::mutex mutex;
};

struct ShaderPayload {
    ShaderPayload(ShaderDesc value, std::shared_ptr<void> nativeValue = {})
        : desc(std::move(value)), reflection(desc.reflection),
          native(std::move(nativeValue)) {}
    ShaderDesc desc;
    PipelineReflection reflection;
    std::shared_ptr<void> native;
};

struct PipelinePayload {
    PipelineReflection reflection;
    std::shared_ptr<PipelineLayoutPayload> layout;
    std::shared_ptr<ShaderPayload> vertexShader;
    std::shared_ptr<ShaderPayload> fragmentShader;
    std::shared_ptr<PipelineCachePayload> cache;
    PipelineDesc desc;
    std::shared_ptr<void> native;
};

struct ComputePipelinePayload {
    PipelineReflection reflection;
    std::shared_ptr<PipelineLayoutPayload> layout;
    std::shared_ptr<ShaderPayload> computeShader;
    std::shared_ptr<PipelineCachePayload> cache;
    ComputePipelineDesc desc;
    Extent3D preferredWorkgroupSize{64, 1, 1};
    Extent3D requiredWorkgroupSize{1, 1, 1};
    std::shared_ptr<void> native;
};

struct FencePayload {
    explicit FencePayload(std::uint64_t initial) : value(initial) {}
    std::uint64_t value = 0;
    std::mutex mutex;
    std::condition_variable changed;
};

struct SemaphorePayload {
    explicit SemaphorePayload(std::uint64_t initial,
                              std::shared_ptr<void> nativeValue = {})
        : value(initial), native(std::move(nativeValue)) {}
    std::atomic<std::uint64_t> value{0};
    std::shared_ptr<void> native;
    std::mutex mutex;
    std::condition_variable changed;
};

struct QueryPoolPayload {
    QueryPoolDesc desc;
};

struct SurfacePayload {
    SurfaceDesc desc;
    std::shared_ptr<DevicePayload> device;
    std::shared_ptr<void> native;
};

struct SwapchainPayload {
    SwapchainDesc desc;
    std::shared_ptr<DevicePayload> device;
    std::shared_ptr<TexturePayload> image;
    std::shared_ptr<void> native;
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
    [[nodiscard]] static Sampler sampler(const std::shared_ptr<Runtime>& runtime,
                                         Handle handle) {
        return Sampler{make_state(runtime, ObjectKind::sampler, handle)};
    }
    [[nodiscard]] static BindGroupLayout bind_group_layout(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return BindGroupLayout{
            make_state(runtime, ObjectKind::bind_group_layout, handle)};
    }
    [[nodiscard]] static DescriptorArena descriptor_arena(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return DescriptorArena{
            make_state(runtime, ObjectKind::descriptor_arena, handle)};
    }
    [[nodiscard]] static BindGroup bind_group(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return BindGroup{make_state(runtime, ObjectKind::bind_group, handle)};
    }
    [[nodiscard]] static BindlessTable bindless_table(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return BindlessTable{
            make_state(runtime, ObjectKind::bindless_table, handle)};
    }
    [[nodiscard]] static PipelineLayout pipeline_layout(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return PipelineLayout{
            make_state(runtime, ObjectKind::pipeline_layout, handle)};
    }
    [[nodiscard]] static PipelineCache pipeline_cache(
        const std::shared_ptr<Runtime>& runtime, Handle handle) {
        return PipelineCache{
            make_state(runtime, ObjectKind::pipeline_cache, handle)};
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
    info.platform = runtime.config.platform;
    info.maturity = runtime.config.maturity;
    info.native = runtime.config.native;
    info.validationOnly = runtime.config.validationOnly;
    info.presentation = runtime.config.presentation;
    info.queueKinds = runtime.config.queueKinds;
    info.supportedFeatures = runtime.config.supportedFeatures;
    info.resources = runtime.config.resourceCapabilities;
    info.bindings = runtime.config.bindingCapabilities;
    info.pipelines = runtime.config.pipelineCapabilities;
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
    payload->device = device;
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
        auto nativeResult =
            runtime.config.createBuffer(runtime.config.nativeContext, desc);
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
        auto nativeResult =
            runtime.config.createTexture(runtime.config.nativeContext, desc);
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

[[nodiscard]] Result<Handle> foundation_create_sampler(
    Runtime& runtime, Handle deviceHandle, const SamplerDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.bindings.ordinaryBindGroups) {
        return unsupported(runtime, "sampler creation");
    }
    if (!std::isfinite(desc.lodMin) || !std::isfinite(desc.lodMax) ||
        !std::isfinite(desc.maxAnisotropy) || desc.lodMin < 0.0F ||
        desc.lodMax < desc.lodMin || desc.maxAnisotropy < 1.0F) {
        return Status::failure(StatusCode::invalid_argument,
                               "sampler LOD or anisotropy is invalid");
    }
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createSampler == nullptr) {
            return unsupported(runtime, "sampler creation");
        }
        auto result = runtime.config.createSampler(runtime.config.nativeContext,
                                                   desc);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    auto payload = std::make_shared<SamplerPayload>();
    payload->desc = desc;
    payload->native = std::move(native);
    runtime.update_stats([](BackendStats& stats) { ++stats.samplersCreated; });
    return runtime.allocate(ObjectKind::sampler, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_bind_group_layout(
    Runtime& runtime, Handle deviceHandle, const BindGroupLayoutDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    const auto& capabilities = device->adapter.bindings;
    if (!capabilities.ordinaryBindGroups) {
        return unsupported(runtime, "bind-group layout creation");
    }
    if (desc.group >= capabilities.maxBindGroups ||
        desc.entries.size() > capabilities.maxBindingsPerGroup) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind-group layout exceeds device limits");
    }
    auto payload = std::make_shared<BindGroupLayoutPayload>();
    payload->desc = desc;
    std::sort(payload->desc.entries.begin(), payload->desc.entries.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.binding < rhs.binding;
              });
    std::uint64_t descriptors = 0;
    for (std::size_t index = 0; index < payload->desc.entries.size(); ++index) {
        auto& entry = payload->desc.entries[index];
        descriptors += entry.arrayCount;
        if (entry.arrayCount == 0 ||
            (entry.arrayCount > 1 && !capabilities.descriptorArrays) ||
            (entry.dynamicOffset && !capabilities.dynamicOffsets) ||
            (entry.dynamicOffset &&
             entry.type != BindingType::uniform_buffer &&
             entry.type != BindingType::storage_buffer) ||
            (entry.minimumBufferSize != 0 &&
             entry.type != BindingType::uniform_buffer &&
             entry.type != BindingType::storage_buffer) ||
            entry.visibility == ShaderStageMask::none ||
            (index != 0 && payload->desc.entries[index - 1].binding ==
                               entry.binding)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "bind-group layout entry is invalid");
        }
        if (entry.immutableSampler != nullptr) {
            if (entry.type != BindingType::sampler || entry.arrayCount != 1 ||
                !capabilities.immutableSamplers ||
                !entry.immutableSampler->valid()) {
                return Status::failure(StatusCode::invalid_argument,
                                       "immutable sampler entry is invalid");
            }
            const auto sampler = runtime.resolve<SamplerPayload>(
                ObjectKind::sampler, entry.immutableSampler->id().value);
            if (!sampler) {
                return invalid_object("immutable sampler");
            }
            payload->immutableSamplers.emplace_back(entry.binding, sampler);
            entry.immutableSampler = nullptr;
        }
    }
    if (descriptors > capabilities.maxDescriptorsPerGroup) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind-group layout exceeds descriptor limit");
    }
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.bindGroupLayoutsCreated; });
    return runtime.allocate(ObjectKind::bind_group_layout, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_descriptor_arena(
    Runtime& runtime, Handle deviceHandle, const DescriptorArenaDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.bindings.ordinaryBindGroups) {
        return unsupported(runtime, "descriptor arena creation");
    }
    if (desc.maxBindGroups == 0 || desc.maxDescriptors == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "descriptor arena limits must be non-zero");
    }
    if (desc.updateAfterBind && !device->adapter.bindings.updateAfterBind) {
        return unsupported(runtime, "update-after-bind descriptor arena");
    }
    auto payload = std::make_shared<DescriptorArenaPayload>();
    payload->desc = desc;
    payload->owner = std::this_thread::get_id();
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.descriptorArenasCreated; });
    return runtime.allocate(ObjectKind::descriptor_arena, std::move(payload));
}

[[nodiscard]] const BindGroupLayoutEntry* find_layout_entry(
    const BindGroupLayoutPayload& layout, std::uint32_t binding) {
    const auto found = std::lower_bound(
        layout.desc.entries.begin(), layout.desc.entries.end(), binding,
        [](const auto& entry, std::uint32_t value) {
            return entry.binding < value;
        });
    return found != layout.desc.entries.end() && found->binding == binding
               ? &*found
               : nullptr;
}

[[nodiscard]] std::shared_ptr<SamplerPayload> find_immutable_sampler(
    const BindGroupLayoutPayload& layout, std::uint32_t binding) {
    const auto found = std::find_if(
        layout.immutableSamplers.begin(), layout.immutableSamplers.end(),
        [&](const auto& candidate) { return candidate.first == binding; });
    return found != layout.immutableSamplers.end() ? found->second
                                                   : nullptr;
}

[[nodiscard]] Result<Handle> foundation_create_bind_group(
    Runtime& runtime, Handle deviceHandle, const BindGroupDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.bindings.ordinaryBindGroups) {
        return unsupported(runtime, "bind-group creation");
    }
    if (desc.layout == nullptr || desc.arena == nullptr ||
        !desc.layout->valid() || !desc.arena->valid()) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group requires a layout and arena");
    }
    const auto layout = runtime.resolve<BindGroupLayoutPayload>(
        ObjectKind::bind_group_layout, desc.layout->id().value);
    const auto arena = runtime.resolve<DescriptorArenaPayload>(
        ObjectKind::descriptor_arena, desc.arena->id().value);
    if (!layout || !arena) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind-group layout or arena is foreign");
    }
    auto payload = std::make_shared<BindGroupPayload>();
    payload->layoutHandle = desc.layout->id().value;
    payload->layout = layout;
    payload->arena = arena;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
    seen.reserve(desc.entries.size());
    for (const auto& entry : desc.entries) {
        const auto* layoutEntry = find_layout_entry(*layout, entry.binding);
        if (layoutEntry == nullptr ||
            entry.arrayElement >= layoutEntry->arrayCount ||
            std::find(seen.begin(), seen.end(),
                      std::pair{entry.binding, entry.arrayElement}) != seen.end()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "bind-group entry identity is invalid");
        }
        seen.emplace_back(entry.binding, entry.arrayElement);
        BoundResource resource;
        resource.entry = entry;
        resource.type = layoutEntry->type;
        switch (layoutEntry->type) {
        case BindingType::uniform_buffer:
        case BindingType::storage_buffer: {
            if (entry.buffer == nullptr || !entry.buffer->valid() ||
                entry.textureView != nullptr || entry.sampler != nullptr) {
                return Status::failure(StatusCode::invalid_argument,
                                       "buffer binding resource is invalid");
            }
            resource.buffer = runtime.resolve<BufferPayload>(
                ObjectKind::buffer, entry.buffer->id().value);
            if (!resource.buffer) {
                return invalid_object("bind-group buffer");
            }
            if (resource.entry.size == whole_size) {
                resource.entry.size =
                    resource.entry.offset <= resource.buffer->desc.size
                        ? resource.buffer->desc.size - resource.entry.offset
                        : 0;
            }
            const auto requiredUsage =
                layoutEntry->type == BindingType::uniform_buffer
                    ? BufferUsage::uniform
                    : BufferUsage::storage;
            if (!has_usage(resource.buffer->desc.usage, requiredUsage) ||
                !buffer_range_valid(*resource.buffer, resource.entry.offset,
                                    resource.entry.size) ||
                resource.entry.size < layoutEntry->minimumBufferSize) {
                return Status::failure(StatusCode::invalid_argument,
                                       "buffer binding usage or range is invalid");
            }
            break;
        }
        case BindingType::sampled_texture:
        case BindingType::storage_texture:
            if (entry.textureView == nullptr || !entry.textureView->valid() ||
                entry.buffer != nullptr || entry.sampler != nullptr) {
                return Status::failure(StatusCode::invalid_argument,
                                       "texture binding resource is invalid");
            }
            resource.textureView = runtime.resolve<TextureViewPayload>(
                ObjectKind::texture_view, entry.textureView->id().value);
            if (!resource.textureView) {
                return invalid_object("bind-group texture view");
            }
            if (!has_usage(
                    resource.textureView->texture->desc.usage,
                    layoutEntry->type == BindingType::sampled_texture
                        ? TextureUsage::sampled
                        : TextureUsage::storage)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "texture binding usage is invalid");
            }
            break;
        case BindingType::sampler:
            if (!find_immutable_sampler(*layout, entry.binding)) {
                if (entry.sampler == nullptr || !entry.sampler->valid() ||
                    entry.buffer != nullptr || entry.textureView != nullptr) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "sampler binding resource is invalid");
                }
                resource.sampler = runtime.resolve<SamplerPayload>(
                    ObjectKind::sampler, entry.sampler->id().value);
                if (!resource.sampler) {
                    return invalid_object("bind-group sampler");
                }
            } else if (entry.sampler != nullptr || entry.buffer != nullptr ||
                       entry.textureView != nullptr) {
                return Status::failure(StatusCode::invalid_argument,
                                       "immutable sampler binding has a resource");
            }
            break;
        }
        payload->resources.push_back(std::move(resource));
    }
    std::uint32_t expectedEntries = 0;
    for (const auto& entry : layout->desc.entries) {
        if (!find_immutable_sampler(*layout, entry.binding)) {
            expectedEntries += entry.arrayCount;
        }
    }
    if (payload->resources.size() != expectedEntries) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group does not populate its complete layout");
    }
    {
        std::lock_guard lock{arena->mutex};
        if (arena->owner != std::this_thread::get_id()) {
            return Status::failure(StatusCode::invalid_state,
                                   "descriptor arena is owned by another thread");
        }
        if (arena->allocatedBindGroups >= arena->desc.maxBindGroups ||
            payload->resources.size() >
                arena->desc.maxDescriptors - arena->allocatedDescriptors) {
            return Status::failure(StatusCode::out_of_memory,
                                   "descriptor arena capacity is exhausted");
        }
        payload->arenaEpoch = arena->epoch;
        ++arena->allocatedBindGroups;
        arena->allocatedDescriptors +=
            static_cast<std::uint32_t>(payload->resources.size());
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.bindGroupsCreated; });
    return runtime.allocate(ObjectKind::bind_group, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_pipeline_layout(
    Runtime& runtime, Handle deviceHandle, const PipelineLayoutDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.bindings.ordinaryBindGroups) {
        return unsupported(runtime, "pipeline-layout creation");
    }
    if (desc.bindGroupLayouts.size() > device->adapter.bindings.maxBindGroups) {
        return Status::failure(StatusCode::invalid_argument,
                               "pipeline layout exceeds bind-group limit");
    }
    auto payload = std::make_shared<PipelineLayoutPayload>();
    payload->desc = desc;
    std::uint32_t previousGroup = 0;
    bool first = true;
    for (auto* layoutObject : desc.bindGroupLayouts) {
        if (layoutObject == nullptr || !layoutObject->valid()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "pipeline layout contains invalid bind-group layout");
        }
        const auto layout = runtime.resolve<BindGroupLayoutPayload>(
            ObjectKind::bind_group_layout, layoutObject->id().value);
        if (!layout || (!first && layout->desc.group <= previousGroup)) {
            return Status::failure(
                StatusCode::invalid_argument,
                "pipeline bind-group layouts must be foreign-free and group sorted");
        }
        first = false;
        previousGroup = layout->desc.group;
        payload->layoutHandles.push_back(layoutObject->id().value);
        payload->layouts.push_back(layout);
    }
    payload->desc.bindGroupLayouts.clear();
    std::uint64_t pushBytes = 0;
    for (std::size_t index = 0; index < desc.pushConstants.size(); ++index) {
        const auto& range = desc.pushConstants[index];
        if (range.size == 0 || range.offset % 4 != 0 || range.size % 4 != 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "pipeline push-constant range is invalid");
        }
        const auto end =
            static_cast<std::uint64_t>(range.offset) + range.size;
        for (std::size_t previousIndex = 0; previousIndex < index;
             ++previousIndex) {
            const auto& previous = desc.pushConstants[previousIndex];
            if (previous.stage != range.stage) {
                continue;
            }
            const auto previousEnd =
                static_cast<std::uint64_t>(previous.offset) + previous.size;
            if (range.offset < previousEnd && previous.offset < end) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "pipeline push-constant ranges overlap for one stage");
            }
        }
        pushBytes = std::max(pushBytes, static_cast<std::uint64_t>(range.offset) +
                                           range.size);
    }
    if (!desc.pushConstants.empty() &&
        !device->adapter.bindings.pushConstants) {
        return unsupported(runtime, "pipeline push-constant layout");
    }
    if (pushBytes > device->adapter.bindings.maxPushConstantBytes) {
        return Status::failure(StatusCode::invalid_argument,
                               "pipeline push-constant layout exceeds device limit");
    }
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.pipelineLayoutsCreated; });
    return runtime.allocate(ObjectKind::pipeline_layout, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_bindless_table(
    Runtime& runtime, Handle deviceHandle, const BindlessTableDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.bindings.bindlessTables ||
        !device->adapter.bindings.updateAfterBind) {
        return unsupported(runtime, "bindless descriptor tables");
    }
    if (desc.layout == nullptr || !desc.layout->valid() || desc.capacity == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "bindless table descriptor is invalid");
    }
    return unsupported(runtime, "bindless descriptor tables");
}

[[nodiscard]] Result<Handle> foundation_create_pipeline_cache(
    Runtime& runtime, Handle deviceHandle, const PipelineCacheDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.pipelines.pipelineCache) {
        return unsupported(runtime, "pipeline cache creation");
    }
    auto payload = std::make_shared<PipelineCachePayload>();
    payload->data = desc.initialData;
    runtime.update_stats(
        [](BackendStats& stats) { ++stats.pipelineCachesCreated; });
    return runtime.allocate(ObjectKind::pipeline_cache, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_shader(Runtime& runtime,
                                                      Handle deviceHandle,
                                                      const ShaderDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    if (desc.entryPoint.empty() || desc.code.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "shader entry point and code are required");
    }
    for (std::size_t index = 0; index < desc.reflection.size(); ++index) {
        const auto& binding = desc.reflection[index];
        if (binding.stage != desc.stage || binding.arrayCount == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader resource reflection is invalid");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (desc.reflection[previous].group == binding.group &&
                desc.reflection[previous].binding == binding.binding) {
                return Status::failure(StatusCode::invalid_argument,
                                       "shader resource reflection is duplicated");
            }
        }
    }
    for (std::size_t index = 0; index < desc.pushConstants.size(); ++index) {
        const auto& range = desc.pushConstants[index];
        if (range.stage != desc.stage || range.size == 0 ||
            range.offset % 4 != 0 || range.size % 4 != 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader push-constant reflection is invalid");
        }
        const auto end = static_cast<std::uint64_t>(range.offset) + range.size;
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto& candidate = desc.pushConstants[previous];
            const auto candidateEnd =
                static_cast<std::uint64_t>(candidate.offset) + candidate.size;
            if (range.offset < candidateEnd && candidate.offset < end) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "shader push-constant reflection ranges overlap");
            }
        }
    }
    for (std::size_t index = 0; index < desc.specializationConstants.size();
         ++index) {
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (desc.specializationConstants[previous].id ==
                desc.specializationConstants[index].id) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "shader specialization-constant ids are duplicated");
            }
        }
    }
    for (std::size_t index = 0; index < desc.bindingMap.size(); ++index) {
        const auto& mapping = desc.bindingMap[index];
        const auto reflected = std::find_if(
            desc.reflection.begin(), desc.reflection.end(),
            [&](const ResourceBinding& binding) {
                return mapping.stage == desc.stage &&
                       binding.group == mapping.group &&
                       binding.binding == mapping.binding &&
                       mapping.arrayElement < binding.arrayCount;
            });
        if (reflected == desc.reflection.end()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "shader binding remap has no logical resource");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto& candidate = desc.bindingMap[previous];
            if (candidate.stage == mapping.stage &&
                candidate.group == mapping.group &&
                candidate.binding == mapping.binding &&
                candidate.arrayElement == mapping.arrayElement) {
                return Status::failure(StatusCode::invalid_argument,
                                       "shader binding remap is duplicated");
            }
        }
    }
    if (desc.stage == ShaderStage::compute &&
        (desc.requiredWorkgroupSize.width == 0 ||
         desc.requiredWorkgroupSize.height == 0 ||
         desc.requiredWorkgroupSize.depth == 0 ||
         desc.preferredWorkgroupSize.width == 0 ||
         desc.preferredWorkgroupSize.height == 0 ||
         desc.preferredWorkgroupSize.depth == 0)) {
        return Status::failure(StatusCode::invalid_argument,
                               "compute shader workgroup metadata is invalid");
    }
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createShader == nullptr) {
            return unsupported(runtime, "shader creation");
        }
        auto result = runtime.config.createShader(runtime.config.nativeContext,
                                                  desc);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    runtime.update_stats([](BackendStats& stats) { ++stats.shadersCreated; });
    return runtime.allocate(ObjectKind::shader,
                            std::make_shared<ShaderPayload>(desc,
                                                            std::move(native)));
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

[[nodiscard]] bool push_constant_layout_matches(
    const std::shared_ptr<PipelineLayoutPayload>& layout,
    const std::shared_ptr<ShaderPayload>& shader) {
    if (!shader) {
        return true;
    }
    for (const auto& reflected : shader->desc.pushConstants) {
        if (!layout) {
            return false;
        }
        const auto reflectedEnd =
            static_cast<std::uint64_t>(reflected.offset) + reflected.size;
        const auto found = std::find_if(
            layout->desc.pushConstants.begin(), layout->desc.pushConstants.end(),
            [&](const PushConstantRange& range) {
                return range.stage == reflected.stage &&
                       range.offset <= reflected.offset &&
                       reflectedEnd <=
                           static_cast<std::uint64_t>(range.offset) + range.size;
            });
        if (found == layout->desc.pushConstants.end()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool specialization_values_match(
    std::span<const SpecializationValue> values,
    const std::shared_ptr<ShaderPayload>& first,
    const std::shared_ptr<ShaderPayload>& second = {}) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (values[previous].id == values[index].id) {
                return false;
            }
        }
        const auto shader_matches = [&](const std::shared_ptr<ShaderPayload>& shader) {
            if (!shader) {
                return false;
            }
            const auto found = std::find_if(
                shader->desc.specializationConstants.begin(),
                shader->desc.specializationConstants.end(),
                [&](const ShaderSpecializationConstant& constant) {
                    return constant.id == values[index].id;
                });
            return found != shader->desc.specializationConstants.end() &&
                   found->type == values[index].type;
        };
        if (!shader_matches(first) && !shader_matches(second)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr std::uint32_t vertex_format_size(
    VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::float32:
    case VertexFormat::uint32:
        return 4;
    case VertexFormat::float32x2:
    case VertexFormat::uint32x2:
        return 8;
    case VertexFormat::float32x3:
    case VertexFormat::uint32x3:
        return 12;
    case VertexFormat::float32x4:
    case VertexFormat::uint32x4:
        return 16;
    }
    return 0;
}

[[nodiscard]] Result<Handle> foundation_create_pipeline(Runtime& runtime,
                                                        Handle deviceHandle,
                                                        const PipelineDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.pipelines.graphics) {
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
    std::shared_ptr<PipelineLayoutPayload> layout;
    if (desc.layout != nullptr) {
        layout = runtime.resolve<PipelineLayoutPayload>(
            ObjectKind::pipeline_layout, desc.layout->id().value);
        if (!layout) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics pipeline layout is foreign");
        }
    }
    const auto layout_matches = [&](const std::shared_ptr<ShaderPayload>& shader) {
        if (!shader) {
            return true;
        }
        for (const auto& binding : shader->desc.reflection) {
            if (!layout) {
                return false;
            }
            const auto layoutFound = std::find_if(
                layout->layouts.begin(), layout->layouts.end(),
                [&](const auto& candidate) {
                    return candidate->desc.group == binding.group;
                });
            if (layoutFound == layout->layouts.end()) {
                return false;
            }
            const auto* entry = find_layout_entry(**layoutFound, binding.binding);
            if (entry == nullptr || entry->arrayCount < binding.arrayCount ||
                !has_stage(entry->visibility, shader_stage_mask(binding.stage))) {
                return false;
            }
            const auto compatible =
                (binding.type == ResourceBindingType::buffer &&
                 (entry->type == BindingType::uniform_buffer ||
                  entry->type == BindingType::storage_buffer)) ||
                (binding.type == ResourceBindingType::texture &&
                 (entry->type == BindingType::sampled_texture ||
                  entry->type == BindingType::storage_texture)) ||
                (binding.type == ResourceBindingType::sampler &&
                 entry->type == BindingType::sampler);
            if (!compatible || entry->minimumBufferSize < binding.minimumSize) {
                return false;
            }
        }
        return true;
    };
    if (!layout_matches(vertex) || !layout_matches(fragment) ||
        !push_constant_layout_matches(layout, vertex) ||
        !push_constant_layout_matches(layout, fragment)) {
        return Status::failure(StatusCode::invalid_argument,
                               "pipeline layout does not match shader reflection");
    }
    if (!specialization_values_match(desc.specializationConstants, vertex,
                                     fragment)) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics specialization constants are invalid");
    }
    if (desc.colorTargets.size() > device->adapter.pipelines.maxColorAttachments ||
        (desc.colorTargets.size() > 1 &&
         !device->adapter.pipelines.multipleRenderTargets) ||
        (desc.depthStencil.format != TextureFormat::unknown &&
         !device->adapter.pipelines.depthStencil) ||
        (desc.multisample.sampleCount > 1 &&
         !device->adapter.pipelines.multisample) ||
        desc.vertexBuffers.size() > device->adapter.pipelines.maxVertexBuffers ||
        desc.multisample.sampleCount == 0 ||
        (desc.topology == PrimitiveTopology::patch_list &&
         !device->adapter.pipelines.tessellation)) {
        return Status::failure(StatusCode::unsupported,
                               "graphics pipeline state exceeds backend capabilities");
    }
    if ((desc.topology == PrimitiveTopology::patch_list &&
         desc.patchControlPoints == 0) ||
        (desc.topology != PrimitiveTopology::patch_list &&
         desc.patchControlPoints != 0) ||
        (desc.multisample.sampleCount != 1 &&
         desc.multisample.sampleCount != 2 &&
         desc.multisample.sampleCount != 4 &&
         desc.multisample.sampleCount != 8) ||
        desc.viewports.size() > device->adapter.pipelines.maxViewports ||
        desc.scissors.size() > device->adapter.pipelines.maxViewports ||
        (!desc.viewports.empty() && !desc.scissors.empty() &&
         desc.viewports.size() != desc.scissors.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics pipeline topology or raster state is invalid");
    }
    for (const auto& target : desc.colorTargets) {
        const auto info = format_info(target.format);
        if (!has_aspect(info.aspects, TextureAspect::color) ||
            target.writeMask > 0x0f) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics color target state is invalid");
        }
    }
    if (desc.depthStencil.format != TextureFormat::unknown &&
        !has_aspect(format_info(desc.depthStencil.format).aspects,
                    TextureAspect::depth)) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics depth-stencil format is invalid");
    }
    std::vector<std::uint32_t> vertexLocations;
    for (const auto& buffer : desc.vertexBuffers) {
        if (buffer.stride == 0 || buffer.attributes.empty()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "vertex-buffer layout is empty");
        }
        for (const auto& attribute : buffer.attributes) {
            const auto size = vertex_format_size(attribute.format);
            if (size == 0 || attribute.offset > buffer.stride ||
                size > buffer.stride - attribute.offset ||
                std::find(vertexLocations.begin(), vertexLocations.end(),
                          attribute.location) != vertexLocations.end()) {
                return Status::failure(StatusCode::invalid_argument,
                                       "vertex attribute layout is invalid");
            }
            vertexLocations.push_back(attribute.location);
        }
    }
    for (const auto& viewport : desc.viewports) {
        if (viewport.width <= 0.0F || viewport.height <= 0.0F ||
            viewport.minimumDepth < 0.0F || viewport.maximumDepth > 1.0F ||
            viewport.minimumDepth > viewport.maximumDepth) {
            return Status::failure(StatusCode::invalid_argument,
                                   "static viewport is invalid");
        }
    }
    for (const auto& scissor : desc.scissors) {
        if (scissor.x < 0 || scissor.y < 0 || scissor.width == 0 ||
            scissor.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "static scissor is invalid");
        }
    }
    if (!std::all_of(desc.blendConstant.begin(), desc.blendConstant.end(),
                     [](float component) { return std::isfinite(component); }) ||
        !std::isfinite(desc.rasterization.depthBias) ||
        !std::isfinite(desc.rasterization.depthBiasSlopeScale) ||
        !std::isfinite(desc.rasterization.depthBiasClamp)) {
        return Status::failure(StatusCode::invalid_argument,
                               "graphics state contains a non-finite value");
    }
    std::shared_ptr<PipelineCachePayload> cache;
    if (desc.cache != nullptr) {
        cache = runtime.resolve<PipelineCachePayload>(
            ObjectKind::pipeline_cache, desc.cache->id().value);
        if (!cache) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics pipeline cache is foreign");
        }
    }
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (!vertex || !fragment || runtime.config.createPipeline == nullptr) {
            return Status::failure(StatusCode::invalid_argument,
                                   "native graphics pipeline requires shaders");
        }
        NativePipelineLayout nativeLayout;
        if (layout) {
            nativeLayout.pushConstants = layout->desc.pushConstants;
            for (const auto& group : layout->layouts) {
                nativeLayout.bindGroups.push_back(group->desc);
            }
        }
        auto result = runtime.config.createPipeline(
            runtime.config.nativeContext, desc, nativeLayout, vertex->native,
            fragment->native);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    auto payload = std::make_shared<PipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(vertex, fragment)};
    payload->layout = std::move(layout);
    payload->vertexShader = vertex;
    payload->fragmentShader = fragment;
    payload->cache = std::move(cache);
    payload->desc = desc;
    payload->desc.vertexShader = nullptr;
    payload->desc.fragmentShader = nullptr;
    payload->desc.layout = nullptr;
    payload->desc.cache = nullptr;
    payload->native = std::move(native);
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_compute_pipeline(
    Runtime& runtime, Handle deviceHandle, const ComputePipelineDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    if (!device->adapter.pipelines.compute) {
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
    std::shared_ptr<PipelineLayoutPayload> layout;
    if (desc.layout != nullptr) {
        layout = runtime.resolve<PipelineLayoutPayload>(
            ObjectKind::pipeline_layout, desc.layout->id().value);
        if (!layout) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline layout is foreign");
        }
    }
    for (const auto& binding : shader->desc.reflection) {
        if (!layout) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline layout misses shader group");
        }
        const auto group = std::find_if(
            layout->layouts.begin(), layout->layouts.end(),
            [&](const auto& value) {
                return value->desc.group == binding.group;
            });
        if (group == layout->layouts.end()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline layout misses shader group");
        }
        const auto* entry = find_layout_entry(**group, binding.binding);
        const auto compatible =
            entry != nullptr &&
            ((binding.type == ResourceBindingType::buffer &&
              (entry->type == BindingType::uniform_buffer ||
               entry->type == BindingType::storage_buffer)) ||
             (binding.type == ResourceBindingType::texture &&
              (entry->type == BindingType::sampled_texture ||
               entry->type == BindingType::storage_texture)) ||
             (binding.type == ResourceBindingType::sampler &&
              entry->type == BindingType::sampler));
        if (!compatible || entry->arrayCount < binding.arrayCount ||
            entry->minimumBufferSize < binding.minimumSize ||
            !has_stage(entry->visibility, ShaderStageMask::compute)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline layout mismatches reflection");
        }
    }
    if (!push_constant_layout_matches(layout, shader) ||
        !specialization_values_match(desc.specializationConstants, shader)) {
        return Status::failure(
            StatusCode::invalid_argument,
            "compute pipeline constants do not match shader reflection");
    }
    const auto all_zero = [](Extent3D extent) {
        return extent.width == 0 && extent.height == 0 && extent.depth == 0;
    };
    const auto any_zero = [](Extent3D extent) {
        return extent.width == 0 || extent.height == 0 || extent.depth == 0;
    };
    ComputePipelineDesc normalized = desc;
    if (all_zero(normalized.requiredWorkgroupSize)) {
        normalized.requiredWorkgroupSize = shader->desc.requiredWorkgroupSize;
    } else if (any_zero(normalized.requiredWorkgroupSize) ||
               normalized.requiredWorkgroupSize !=
                   shader->desc.requiredWorkgroupSize) {
        return Status::failure(
            StatusCode::invalid_argument,
            "compute pipeline required workgroup size mismatches the shader");
    }
    if (all_zero(normalized.preferredWorkgroupSize)) {
        normalized.preferredWorkgroupSize =
            shader->desc.preferredWorkgroupSize;
    }
    const auto maxSize = device->adapter.pipelines.maxComputeWorkgroupSize;
    const auto invocations =
        static_cast<std::uint64_t>(normalized.requiredWorkgroupSize.width) *
        normalized.requiredWorkgroupSize.height *
        normalized.requiredWorkgroupSize.depth;
    if (any_zero(normalized.preferredWorkgroupSize) ||
        normalized.requiredWorkgroupSize.width > maxSize.width ||
        normalized.requiredWorkgroupSize.height > maxSize.height ||
        normalized.requiredWorkgroupSize.depth > maxSize.depth ||
        invocations > device->adapter.pipelines.maxComputeInvocations) {
        return Status::failure(StatusCode::unsupported,
                               "compute workgroup metadata exceeds device limits");
    }
    std::shared_ptr<PipelineCachePayload> cache;
    if (desc.cache != nullptr) {
        cache = runtime.resolve<PipelineCachePayload>(
            ObjectKind::pipeline_cache, desc.cache->id().value);
        if (!cache) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline cache is foreign");
        }
    }
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createComputePipeline == nullptr) {
            return unsupported(runtime, "compute pipeline creation");
        }
        NativePipelineLayout nativeLayout;
        if (layout) {
            nativeLayout.pushConstants = layout->desc.pushConstants;
            for (const auto& group : layout->layouts) {
                nativeLayout.bindGroups.push_back(group->desc);
            }
        }
        auto result = runtime.config.createComputePipeline(
            runtime.config.nativeContext, normalized, nativeLayout,
            shader->native);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    auto payload = std::make_shared<ComputePipelinePayload>();
    payload->reflection = PipelineReflection{merge_reflection(shader)};
    payload->layout = std::move(layout);
    payload->computeShader = shader;
    payload->cache = std::move(cache);
    payload->desc = normalized;
    payload->desc.computeShader = nullptr;
    payload->desc.layout = nullptr;
    payload->desc.cache = nullptr;
    payload->preferredWorkgroupSize = normalized.preferredWorkgroupSize;
    payload->requiredWorkgroupSize = normalized.requiredWorkgroupSize;
    payload->native = std::move(native);
    runtime.update_stats([](BackendStats& stats) { ++stats.pipelinesCreated; });
    return runtime.allocate(ObjectKind::compute_pipeline, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_fence(Runtime& runtime,
                                                     Handle deviceHandle,
                                                     const FenceDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    return runtime.allocate(ObjectKind::fence,
                            std::make_shared<FencePayload>(desc.initialValue));
}

[[nodiscard]] Result<Handle> foundation_create_semaphore(
    Runtime& runtime, Handle deviceHandle, const SemaphoreDesc& desc) {
    if (!runtime.resolve<DevicePayload>(ObjectKind::device, deviceHandle)) {
        return invalid_object("device");
    }
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createSemaphore == nullptr) {
            return unsupported(runtime, "semaphore creation");
        }
        auto result = runtime.config.createSemaphore(desc);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    return runtime.allocate(
        ObjectKind::semaphore,
        std::make_shared<SemaphorePayload>(desc.initialValue,
                                           std::move(native)));
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
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                        deviceHandle);
    if (!device) {
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
    std::shared_ptr<void> native;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createSurface == nullptr) {
            return unsupported(runtime, "surface creation");
        }
        auto result = runtime.config.createSurface(desc);
        if (!result.ok()) {
            return result.status();
        }
        native = std::move(result).value();
    }
    auto payload = std::make_shared<SurfacePayload>();
    payload->desc = desc;
    payload->device = device;
    payload->native = std::move(native);
    runtime.update_stats([](BackendStats& stats) { ++stats.surfacesCreated; });
    return runtime.allocate(ObjectKind::surface, std::move(payload));
}

[[nodiscard]] Result<Handle> foundation_create_swapchain(
    Runtime& runtime, Handle deviceHandle, Handle surfaceHandle,
    const SwapchainDesc& desc) {
    const auto device = runtime.resolve<DevicePayload>(ObjectKind::device,
                                                       deviceHandle);
    if (!device) {
        return invalid_object("device");
    }
    const auto surface = runtime.resolve<SurfacePayload>(ObjectKind::surface,
                                                          surfaceHandle);
    if (!surface) {
        return invalid_object("surface");
    }
    if (!validation::is_non_zero(desc.extent) || desc.imageCount == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "swapchain extent and image count must be non-zero");
    }
    auto payload = std::make_shared<SwapchainPayload>();
    payload->desc = desc;
    payload->device = device;
    if (!runtime.config.logicalResources) {
        if (runtime.config.createSwapchain == nullptr) {
            return unsupported(runtime, "swapchain creation");
        }
        auto result = runtime.config.createSwapchain(surface->native, desc);
        if (!result.ok()) {
            if (result.status().code == StatusCode::device_lost) {
                device->lost.store(true);
            }
            return result.status();
        }
        payload->native = std::move(result).value();
        runtime.update_stats(
            [](BackendStats& stats) { ++stats.swapchainsCreated; });
        return runtime.allocate(ObjectKind::swapchain, std::move(payload));
    }
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
    for (auto& sync : payload->image->sync) {
        sync.layout = TextureLayout::present;
        sync.owner = QueueKind::graphics;
        sync.ownerSet = true;
    }
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

[[nodiscard]] Status execute_barrier(
    const CommandListPayload::BarrierCommand& command, QueueKind queueKind) {
    for (const auto& barrier : command.buffers) {
        auto& resource = *barrier.resource;
        const auto size = barrier.desc.size == whole_size
                              ? resource.desc.size - barrier.desc.offset
                              : barrier.desc.size;
        if (!buffer_range_valid(resource, barrier.desc.offset, size)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier range is invalid");
        }
        std::lock_guard lock{resource.mutex};
        if (barrier.desc.transferOwnership) {
            if (barrier.desc.sourceQueue == barrier.desc.destinationQueue ||
                barrier.desc.sourceQueue != queueKind ||
                (resource.sync.ownerSet &&
                 resource.sync.owner != barrier.desc.sourceQueue)) {
                return Status::failure(
                    StatusCode::invalid_state,
                    "buffer barrier queue ownership transfer is invalid");
            }
            resource.sync.owner = barrier.desc.destinationQueue;
            resource.sync.ownerSet = true;
        } else if (resource.sync.ownerSet && resource.sync.owner != queueKind) {
            return Status::failure(StatusCode::invalid_state,
                                   "buffer is owned by another queue");
        } else {
            resource.sync.owner = queueKind;
            resource.sync.ownerSet = true;
        }
        resource.sync.stages = barrier.desc.destinationStages;
        resource.sync.access = barrier.desc.destinationAccess;
    }
    for (const auto& barrier : command.textures) {
        auto& resource = *barrier.resource;
        const auto& range = barrier.desc.range;
        if (range.mipLevelCount == 0 || range.arrayLayerCount == 0 ||
            range.baseMipLevel >= resource.desc.mipLevels ||
            range.mipLevelCount >
                resource.desc.mipLevels - range.baseMipLevel ||
            range.baseArrayLayer >= resource.desc.arrayLayers ||
            range.arrayLayerCount >
                resource.desc.arrayLayers - range.baseArrayLayer ||
            range.aspects == TextureAspect::none ||
            !has_aspect(validation::format_aspects(resource.desc.format),
                        range.aspects)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier range is invalid");
        }
        std::lock_guard lock{resource.mutex};
        for (std::uint32_t layer = range.baseArrayLayer;
             layer < range.baseArrayLayer + range.arrayLayerCount; ++layer) {
            for (std::uint32_t mip = range.baseMipLevel;
                 mip < range.baseMipLevel + range.mipLevelCount; ++mip) {
                auto& sync = resource.sync[static_cast<std::size_t>(layer) *
                                               resource.desc.mipLevels +
                                           mip];
                if (barrier.desc.oldLayout != TextureLayout::undefined &&
                    sync.layout != barrier.desc.oldLayout) {
                    return Status::failure(
                        StatusCode::invalid_state,
                        "texture barrier old layout does not match tracked layout");
                }
                if (barrier.desc.transferOwnership) {
                    if (barrier.desc.sourceQueue ==
                            barrier.desc.destinationQueue ||
                        barrier.desc.sourceQueue != queueKind ||
                        (sync.ownerSet &&
                         sync.owner != barrier.desc.sourceQueue)) {
                        return Status::failure(
                            StatusCode::invalid_state,
                            "texture barrier queue ownership transfer is invalid");
                    }
                    sync.owner = barrier.desc.destinationQueue;
                    sync.ownerSet = true;
                } else if (sync.ownerSet && sync.owner != queueKind) {
                    return Status::failure(StatusCode::invalid_state,
                                           "texture is owned by another queue");
                } else {
                    sync.owner = queueKind;
                    sync.ownerSet = true;
                }
                sync.layout = barrier.desc.newLayout;
                sync.stages = barrier.desc.destinationStages;
                sync.access = barrier.desc.destinationAccess;
            }
        }
    }
    return Status::success();
}

[[nodiscard]] Status foundation_submit(Runtime& runtime, Handle queueHandle,
                                       std::span<const Handle> commandLists,
                                       std::span<const SemaphorePointHandle> waits,
                                       std::span<const SemaphorePointHandle> signals,
                                       Handle fenceHandle,
                                       std::uint64_t fenceValue,
                                       std::chrono::nanoseconds waitTimeout) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    if (!queue->device || queue->device->lost.load()) {
        return Status::failure(StatusCode::device_lost,
                               "submission device is lost");
    }
    std::lock_guard queueLock{queue->submitMutex};
    std::vector<std::shared_ptr<CommandListPayload>> lists;
    std::vector<CommandListPayload::Operation> operations;
    std::vector<std::shared_ptr<SemaphorePayload>> waitSemaphores;
    std::vector<std::shared_ptr<SemaphorePayload>> signalSemaphores;
    std::vector<NativeSemaphorePoint> nativeWaits;
    std::vector<NativeSemaphorePoint> nativeSignals;
    waitSemaphores.reserve(waits.size());
    signalSemaphores.reserve(signals.size());
    nativeWaits.reserve(waits.size());
    nativeSignals.reserve(signals.size());
    for (const auto& wait : waits) {
        auto semaphore = runtime.resolve<SemaphorePayload>(ObjectKind::semaphore,
                                                            wait.semaphore);
        if (!semaphore) {
            return invalid_object("wait semaphore");
        }
        waitSemaphores.push_back(semaphore);
        nativeWaits.push_back({semaphore->native, wait.value, wait.stages});
    }
    for (const auto& signal : signals) {
        auto semaphore = runtime.resolve<SemaphorePayload>(ObjectKind::semaphore,
                                                            signal.semaphore);
        if (!semaphore) {
            return invalid_object("signal semaphore");
        }
        if (signal.value <= semaphore->value.load()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "timeline semaphore signal values must increase");
        }
        signalSemaphores.push_back(semaphore);
        nativeSignals.push_back(
            {semaphore->native, signal.value, PipelineStage::bottom});
    }
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
        operations.insert(operations.end(), list->operations.begin(),
                          list->operations.end());
        lists.push_back(std::move(list));
    }
    for (std::size_t index = 0; index < waits.size(); ++index) {
        auto& semaphore = waitSemaphores[index];
        std::unique_lock semaphoreLock{semaphore->mutex};
        const auto ready = [&] {
            return semaphore->value.load() >= waits[index].value;
        };
        if (waitTimeout == std::chrono::nanoseconds::max()) {
            semaphore->changed.wait(semaphoreLock, ready);
        } else if (!semaphore->changed.wait_for(semaphoreLock, waitTimeout,
                                                ready)) {
            return Status::failure(StatusCode::timeout,
                                   "queue semaphore wait timed out");
        }
    }
    std::size_t transferCount = 0;
    if (runtime.config.logicalResources) {
        for (const auto& operation : operations) {
            switch (operation.kind) {
            case CommandListPayload::OperationKind::command:
                break;
            case CommandListPayload::OperationKind::transfer:
                ++transferCount;
                if (auto status = execute_logical_transfer(operation.transfer);
                    !status.ok()) {
                    return status;
                }
                break;
            case CommandListPayload::OperationKind::barrier:
                if (auto status = execute_barrier(operation.barrier, queue->kind);
                    !status.ok()) {
                    return status;
                }
                break;
            }
        }
    } else {
        if (runtime.config.nativeSubmit == nullptr) {
            return unsupported(runtime, "command submission");
        }
        std::vector<NativeCommand> nativeCommands;
        nativeCommands.reserve(operations.size());
        for (const auto& operation : operations) {
            if (operation.kind == CommandListPayload::OperationKind::barrier) {
                if (auto status = execute_barrier(operation.barrier, queue->kind);
                    !status.ok()) {
                    return status;
                }
            }
            nativeCommands.push_back(operation.native);
            if (operation.kind == CommandListPayload::OperationKind::transfer) {
                ++transferCount;
            }
        }
        if (auto status = runtime.config.nativeSubmit(runtime.config.nativeContext,
                                                      nativeCommands, nativeWaits,
                                                      nativeSignals);
            !status.ok()) {
            if (status.code == StatusCode::device_lost) {
                queue->device->lost.store(true);
            }
            return status;
        }
    }
    for (const auto& list : lists) {
        std::lock_guard listLock{list->mutex};
        list->state = CommandListState::submitted;
        list->retained.clear();
        list->operations.clear();
        list->graphicsPipeline.reset();
        list->computePipeline.reset();
    }
    for (std::size_t index = 0; index < signals.size(); ++index) {
        signalSemaphores[index]->value.store(signals[index].value);
        signalSemaphores[index]->changed.notify_all();
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
    runtime.update_stats([transferCount](BackendStats& stats) {
        ++stats.submissions;
        stats.transfersExecuted += transferCount;
    });
    return Status::success();
}

[[nodiscard]] Status foundation_present(Runtime& runtime, Handle queueHandle,
                                        Handle swapchainHandle,
                                        std::uint32_t imageIndex,
                                        std::span<const SemaphorePointHandle> waits) {
    const auto queue = runtime.resolve<QueuePayload>(ObjectKind::queue, queueHandle);
    const auto swapchain = runtime.resolve<SwapchainPayload>(
        ObjectKind::swapchain, swapchainHandle);
    if (!queue) {
        return invalid_object("queue");
    }
    if (!queue->device || queue->device->lost.load()) {
        return Status::failure(StatusCode::device_lost,
                               "presentation device is lost");
    }
    if (!swapchain) {
        return invalid_object("swapchain");
    }
    if (queue->kind != QueueKind::graphics) {
        return Status::failure(StatusCode::invalid_argument,
                               "presentation requires a graphics queue");
    }
    std::vector<NativeSemaphorePoint> nativeWaits;
    nativeWaits.reserve(waits.size());
    for (const auto& wait : waits) {
        const auto semaphore = runtime.resolve<SemaphorePayload>(
            ObjectKind::semaphore, wait.semaphore);
        if (!semaphore) {
            return invalid_object("present wait semaphore");
        }
        if (semaphore->value.load() < wait.value) {
            return Status::failure(
                StatusCode::invalid_state,
                "present wait semaphore has not reached the requested value");
        }
        nativeWaits.push_back({semaphore->native, wait.value, wait.stages});
    }
    std::lock_guard lock{swapchain->mutex};
    if (!swapchain->acquired || imageIndex >= swapchain->desc.imageCount) {
        return Status::failure(StatusCode::invalid_state,
                               "present requires an acquired swapchain image");
    }
    if (!swapchain->image) {
        return Status::failure(StatusCode::invalid_state,
                               "present has no acquired swapchain texture");
    }
    {
        std::lock_guard imageLock{swapchain->image->mutex};
        for (const auto& sync : swapchain->image->sync) {
            if (sync.layout != TextureLayout::present ||
                (sync.ownerSet && sync.owner != QueueKind::graphics)) {
                return Status::failure(
                    StatusCode::invalid_state,
                    "swapchain image must be graphics-owned in present layout");
            }
        }
    }
    if (!runtime.config.logicalResources) {
        if (runtime.config.presentSwapchain == nullptr) {
            return unsupported(runtime, "presentation");
        }
        const auto status = runtime.config.presentSwapchain(
            swapchain->native, imageIndex, nativeWaits);
        if (status.code == StatusCode::device_lost && swapchain->device) {
            swapchain->device->lost.store(true);
        }
        if (status.ok() || status.code == StatusCode::suboptimal ||
            status.code == StatusCode::out_of_date) {
            swapchain->acquired = false;
            swapchain->image.reset();
            runtime.update_stats(
                [](BackendStats& stats) { ++stats.presentations; });
        }
        return status;
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
    &foundation_create_sampler,
    &foundation_create_bind_group_layout,
    &foundation_create_descriptor_arena,
    &foundation_create_bind_group,
    &foundation_create_bindless_table,
    &foundation_create_pipeline_layout,
    &foundation_create_pipeline_cache,
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
    NativeCommand native;
    native.kind = NativeCommandKind::transfer;
    native.transfer = make_native_transfer(command);
    CommandListPayload::Operation operation;
    operation.kind = CommandListPayload::OperationKind::transfer;
    operation.native = std::move(native);
    operation.transfer = std::move(command);
    list->operations.push_back(std::move(operation));
    return Status::success();
}

void record_native(CommandListPayload& list, NativeCommand command) {
    CommandListPayload::Operation operation;
    operation.kind = CommandListPayload::OperationKind::command;
    operation.native = std::move(command);
    list.operations.push_back(std::move(operation));
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
TRUFFLE_DEFINE_OBJECT_LIFETIME(Sampler)
TRUFFLE_DEFINE_OBJECT_LIFETIME(BindGroupLayout)
TRUFFLE_DEFINE_OBJECT_LIFETIME(DescriptorArena)
TRUFFLE_DEFINE_OBJECT_LIFETIME(BindlessTable)
TRUFFLE_DEFINE_OBJECT_LIFETIME(PipelineLayout)
TRUFFLE_DEFINE_OBJECT_LIFETIME(PipelineCache)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Shader)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Pipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(ComputePipeline)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Fence)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Semaphore)
TRUFFLE_DEFINE_OBJECT_LIFETIME(QueryPool)
TRUFFLE_DEFINE_OBJECT_LIFETIME(Surface)

#undef TRUFFLE_DEFINE_OBJECT_LIFETIME

BindGroup::BindGroup() noexcept = default;
BindGroup::~BindGroup() = default;
BindGroup::BindGroup(BindGroup&&) noexcept = default;
BindGroup& BindGroup::operator=(BindGroup&&) noexcept = default;
BindGroup::BindGroup(std::unique_ptr<detail::ObjectState> state) noexcept
    : state_(std::move(state)) {}

bool BindGroup::valid() const noexcept {
    const auto value = detail::payload<detail::BindGroupPayload>(
        state_, detail::ObjectKind::bind_group);
    if (!value || !value->arena) {
        return false;
    }
    std::lock_guard lock{value->arena->mutex};
    return value->arenaEpoch == value->arena->epoch;
}

ObjectId BindGroup::id() const noexcept {
    return valid() ? detail::state_id(state_) : ObjectId{};
}

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

Result<Sampler> Device::create_sampler(const SamplerDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_sampler(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::sampler(state_->runtime, handle.value());
}

Result<BindGroupLayout> Device::create_bind_group_layout(
    const BindGroupLayoutDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    for (const auto& entry : desc.entries) {
        if (entry.immutableSampler != nullptr &&
            (!entry.immutableSampler->valid() ||
             entry.immutableSampler->state_->runtime.get() !=
                 state_->runtime.get())) {
            return Status::failure(
                StatusCode::invalid_argument,
                "immutable samplers must belong to the device runtime");
        }
    }
    auto handle = state_->runtime->dispatch->create_bind_group_layout(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::bind_group_layout(state_->runtime, handle.value());
}

Result<DescriptorArena> Device::create_descriptor_arena(
    const DescriptorArenaDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_descriptor_arena(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::descriptor_arena(state_->runtime, handle.value());
}

Result<BindGroup> Device::create_bind_group(const BindGroupDesc& desc) const {
    if (!valid() || desc.layout == nullptr || desc.arena == nullptr ||
        !desc.layout->valid() || !desc.arena->valid() ||
        desc.layout->state_->runtime.get() != state_->runtime.get() ||
        desc.arena->state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(
            StatusCode::invalid_argument,
            "bind-group layout and arena must belong to the device runtime");
    }
    for (const auto& entry : desc.entries) {
        const auto foreignBuffer =
            entry.buffer != nullptr &&
            (!entry.buffer->valid() ||
             entry.buffer->state_->runtime.get() != state_->runtime.get());
        const auto foreignTextureView =
            entry.textureView != nullptr &&
            (!entry.textureView->valid() ||
             entry.textureView->state_->runtime.get() != state_->runtime.get());
        const auto foreignSampler =
            entry.sampler != nullptr &&
            (!entry.sampler->valid() ||
             entry.sampler->state_->runtime.get() != state_->runtime.get());
        if (foreignBuffer || foreignTextureView || foreignSampler) {
            return Status::failure(
                StatusCode::invalid_argument,
                "bind-group resources must belong to the device runtime");
        }
    }
    auto handle = state_->runtime->dispatch->create_bind_group(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::bind_group(state_->runtime, handle.value());
}

Result<BindlessTable> Device::create_bindless_table(
    const BindlessTableDesc& desc) const {
    if (!valid() || desc.layout == nullptr || !desc.layout->valid() ||
        desc.layout->state_->runtime.get() != state_->runtime.get()) {
        return Status::failure(
            StatusCode::invalid_argument,
            "bindless table layout must belong to the device runtime");
    }
    auto handle = state_->runtime->dispatch->create_bindless_table(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::bindless_table(state_->runtime, handle.value());
}

Result<PipelineLayout> Device::create_pipeline_layout(
    const PipelineLayoutDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    for (const auto* layout : desc.bindGroupLayouts) {
        if (layout == nullptr || !layout->valid() ||
            layout->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "pipeline bind-group layouts must belong to the device runtime");
        }
    }
    auto handle = state_->runtime->dispatch->create_pipeline_layout(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::pipeline_layout(state_->runtime, handle.value());
}

Result<PipelineCache> Device::create_pipeline_cache(
    const PipelineCacheDesc& desc) const {
    if (!valid()) {
        return detail::invalid_object("device");
    }
    auto handle = state_->runtime->dispatch->create_pipeline_cache(
        *state_->runtime, state_->handle, desc);
    if (!handle.ok()) {
        return handle.status();
    }
    return detail::Factory::pipeline_cache(state_->runtime, handle.value());
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

Result<Shader> Device::create_shader(const ShaderPackage& package,
                                     ShaderTarget target,
                                     std::string_view entryPoint,
                                     ShaderStage stage,
                                     std::string_view permutation) const {
    const auto selected =
        package.select_variant(target, entryPoint, stage, permutation);
    if (!selected.ok()) {
        return selected.status();
    }
    const auto* variant = selected.value();
    try {
        ShaderDesc desc;
        desc.stage = variant->stage;
        desc.format = variant->format;
        desc.entryPoint = variant->entryPoint;
        desc.code = variant->code;
        desc.reflection = variant->reflection.bindings;
        desc.pushConstants = variant->reflection.pushConstants;
        desc.specializationConstants =
            variant->reflection.specializationConstants;
        desc.requiredWorkgroupSize =
            variant->reflection.requiredWorkgroupSize;
        desc.preferredWorkgroupSize =
            variant->reflection.preferredWorkgroupSize;
        desc.debugName = package.desc().name + ":" + variant->entryPoint;
        for (const auto& remap : package.desc().remaps) {
            if (remap.target == target && remap.stage == stage) {
                desc.bindingMap.push_back({
                    .stage = remap.stage,
                    .group = remap.group,
                    .binding = remap.binding,
                    .arrayElement = remap.arrayElement,
                    .nativeGroup = remap.nativeGroup,
                    .nativeBinding = remap.nativeBinding,
                    .nativeArrayElement = remap.nativeArrayElement,
                });
            }
        }
        return create_shader(desc);
    } catch (const std::bad_alloc&) {
        return Status::failure(StatusCode::out_of_memory,
                               "shader package variant allocation failed");
    }
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
    if ((desc.layout != nullptr &&
         (!desc.layout->valid() ||
          desc.layout->state_->runtime.get() != state_->runtime.get())) ||
        (desc.cache != nullptr &&
         (!desc.cache->valid() ||
          desc.cache->state_->runtime.get() != state_->runtime.get()))) {
        return Status::failure(
            StatusCode::invalid_argument,
            "graphics pipeline layout and cache must belong to the device runtime");
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
    if ((desc.layout != nullptr &&
         (!desc.layout->valid() ||
          desc.layout->state_->runtime.get() != state_->runtime.get())) ||
        (desc.cache != nullptr &&
         (!desc.cache->valid() ||
          desc.cache->state_->runtime.get() != state_->runtime.get()))) {
        return Status::failure(
            StatusCode::invalid_argument,
            "compute pipeline layout and cache must belong to the device runtime");
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

SamplerDesc Sampler::desc() const {
    const auto value = detail::payload<detail::SamplerPayload>(
        state_, detail::ObjectKind::sampler);
    return value ? value->desc : SamplerDesc{};
}

DescriptorArenaDesc DescriptorArena::desc() const {
    const auto value = detail::payload<detail::DescriptorArenaPayload>(
        state_, detail::ObjectKind::descriptor_arena);
    return value ? value->desc : DescriptorArenaDesc{};
}

std::uint64_t DescriptorArena::epoch() const noexcept {
    const auto value = detail::payload<detail::DescriptorArenaPayload>(
        state_, detail::ObjectKind::descriptor_arena);
    if (!value) {
        return 0;
    }
    std::lock_guard lock{value->mutex};
    return value->epoch;
}

Status DescriptorArena::reset() {
    const auto value = detail::payload<detail::DescriptorArenaPayload>(
        state_, detail::ObjectKind::descriptor_arena);
    if (!value) {
        return detail::invalid_object("descriptor arena");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id()) {
        return Status::failure(StatusCode::invalid_state,
                               "descriptor arena is owned by another thread");
    }
    ++value->epoch;
    if (value->epoch == 0) {
        value->epoch = 1;
    }
    value->allocatedBindGroups = 0;
    value->allocatedDescriptors = 0;
    return Status::success();
}

ObjectId BindGroup::layout_id() const noexcept {
    const auto value = detail::payload<detail::BindGroupPayload>(
        state_, detail::ObjectKind::bind_group);
    return value && valid() ? ObjectId{value->layoutHandle} : ObjectId{};
}

Result<std::vector<std::byte>> PipelineCache::data() const {
    const auto value = detail::payload<detail::PipelineCachePayload>(
        state_, detail::ObjectKind::pipeline_cache);
    if (!value) {
        return detail::invalid_object("pipeline cache");
    }
    return value->data;
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
    value->indexBufferBound = false;
    value->viewportSet = true;
    value->scissorSet = true;
    value->blendConstantSet = true;
    value->stencilReferenceSet = true;
    value->depthBiasSet = true;
    value->graphicsPipeline.reset();
    value->computePipeline.reset();
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
    value->operations.clear();
    value->graphicsPipelineBound = false;
    value->computePipelineBound = false;
    value->indexBufferBound = false;
    value->viewportSet = true;
    value->scissorSet = true;
    value->blendConstantSet = true;
    value->stencilReferenceSet = true;
    value->depthBiasSet = true;
    value->graphicsPipeline.reset();
    value->computePipeline.reset();
    value->renderColorFormats.clear();
    value->renderDepthStencilFormat = TextureFormat::unknown;
    value->renderSampleCount = 1;
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
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::begin_render;
    command.extent = desc.extent;
    command.colorAttachments.reserve(desc.colorAttachments.size());
    value->renderColorFormats.clear();
    value->renderColorFormats.reserve(desc.colorAttachments.size());
    value->renderDepthStencilFormat = TextureFormat::unknown;
    value->renderSampleCount = 1;
    bool sampleCountSet = false;
    for (const auto& attachment : desc.colorAttachments) {
        if (attachment.texture == nullptr) {
            return Status::failure(StatusCode::invalid_argument,
                                   "render color attachment is null");
        }
        if (!attachment.texture->valid() ||
            attachment.texture->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "render attachments must belong to the command-list runtime");
        }
        auto texture = state_->runtime->resolve<detail::TexturePayload>(
            detail::ObjectKind::texture, attachment.texture->state_->handle);
        if (!texture ||
            !has_usage(texture->desc.usage, TextureUsage::color_attachment) ||
            desc.extent.width > texture->desc.extent.width ||
            desc.extent.height > texture->desc.extent.height) {
            return Status::failure(
                StatusCode::invalid_argument,
                "render color attachment usage or extent is invalid");
        }
        if (sampleCountSet && value->renderSampleCount != texture->desc.sampleCount) {
            return Status::failure(StatusCode::invalid_argument,
                                   "render attachments have unequal sample counts");
        }
        value->renderSampleCount = texture->desc.sampleCount;
        sampleCountSet = true;
        detail::NativeRenderAttachment nativeAttachment;
        nativeAttachment.texture = texture->native;
        nativeAttachment.loadOp = attachment.loadOp;
        nativeAttachment.storeOp = attachment.storeOp;
        nativeAttachment.clear = attachment.clear;
        value->retained.push_back(texture);
        value->renderColorFormats.push_back(texture->desc.format);
        if (attachment.resolveTexture != nullptr) {
            if (!attachment.resolveTexture->valid() ||
                attachment.resolveTexture->state_->runtime.get() !=
                    state_->runtime.get()) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "resolve attachment belongs to another runtime");
            }
            auto resolve = state_->runtime->resolve<detail::TexturePayload>(
                detail::ObjectKind::texture,
                attachment.resolveTexture->state_->handle);
            if (!resolve || texture->desc.sampleCount <= 1 ||
                resolve->desc.sampleCount != 1 ||
                resolve->desc.format != texture->desc.format ||
                !has_usage(resolve->desc.usage,
                           TextureUsage::color_attachment) ||
                desc.extent.width > resolve->desc.extent.width ||
                desc.extent.height > resolve->desc.extent.height) {
                return Status::failure(StatusCode::invalid_argument,
                                       "resolve attachment is incompatible");
            }
            nativeAttachment.resolveTexture = resolve->native;
            value->retained.push_back(std::move(resolve));
        }
        command.colorAttachments.push_back(std::move(nativeAttachment));
    }
    if (desc.depthStencilAttachment.texture != nullptr) {
        auto* attachment = desc.depthStencilAttachment.texture;
        if (!attachment->valid() ||
            attachment->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "depth-stencil attachment belongs to another runtime");
        }
        auto texture = state_->runtime->resolve<detail::TexturePayload>(
            detail::ObjectKind::texture, attachment->state_->handle);
        if (!texture ||
            !has_usage(texture->desc.usage,
                       TextureUsage::depth_stencil_attachment) ||
            desc.extent.width > texture->desc.extent.width ||
            desc.extent.height > texture->desc.extent.height ||
            (sampleCountSet &&
             value->renderSampleCount != texture->desc.sampleCount)) {
            return Status::failure(
                StatusCode::invalid_argument,
                "depth-stencil attachment usage, extent, or samples are invalid");
        }
        value->renderSampleCount = texture->desc.sampleCount;
        value->renderDepthStencilFormat = texture->desc.format;
        const auto& depth = desc.depthStencilAttachment;
        command.depthStencilAttachment = {
            .texture = texture->native,
            .depthLoadOp = depth.depthLoadOp,
            .depthStoreOp = depth.depthStoreOp,
            .clearDepth = depth.clearDepth,
            .stencilLoadOp = depth.stencilLoadOp,
            .stencilStoreOp = depth.stencilStoreOp,
            .clearStencil = depth.clearStencil,
        };
        value->retained.push_back(std::move(texture));
    }
    if (value->renderColorFormats.empty() &&
        value->renderDepthStencilFormat == TextureFormat::unknown &&
        !state_->runtime->config.logicalResources) {
        return Status::failure(StatusCode::invalid_argument,
                               "native render pass requires an attachment");
    }
    detail::record_native(*value, std::move(command));
    value->activeEncoder = 1;
    value->graphicsPipelineBound = false;
    value->indexBufferBound = false;
    value->graphicsPipeline.reset();
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
    value->computePipeline.reset();
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::begin_compute;
    detail::record_native(*value, std::move(command));
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

Status CommandList::barrier(const BarrierBatch& batch) {
    const auto value = detail::payload<detail::CommandListPayload>(
        state_, detail::ObjectKind::command_list);
    if (!value) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{value->mutex};
    if (value->owner != std::this_thread::get_id() ||
        value->state != CommandListState::recording || value->activeEncoder != 0) {
        return Status::failure(
            StatusCode::invalid_state,
            "barriers require an owned recording command list with no active encoder");
    }
    if (batch.buffers.empty() && batch.textures.empty() &&
        batch.aliasing.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "barrier batch must not be empty");
    }
    detail::CommandListPayload::Operation operation;
    operation.kind = detail::CommandListPayload::OperationKind::barrier;
    operation.native.kind = detail::NativeCommandKind::barrier;
    operation.barrier.buffers.reserve(batch.buffers.size());
    operation.barrier.textures.reserve(batch.textures.size());
    operation.barrier.aliasing.reserve(batch.aliasing.size());

    for (auto desc : batch.buffers) {
        if (desc.buffer == nullptr || !desc.buffer->valid() ||
            desc.buffer->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier resource is invalid or foreign");
        }
        if (desc.sourceStages == PipelineStage::none ||
            desc.destinationStages == PipelineStage::none ||
            (desc.transferOwnership &&
             desc.sourceQueue == desc.destinationQueue)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer barrier stages or ownership are invalid");
        }
        auto resource = state_->runtime->resolve<detail::BufferPayload>(
            detail::ObjectKind::buffer, desc.buffer->state_->handle);
        if (!resource) {
            return detail::invalid_object("buffer barrier resource");
        }
        desc.buffer = nullptr;
        operation.barrier.buffers.push_back(
            {.desc = desc, .resource = std::move(resource)});
    }
    for (auto desc : batch.textures) {
        if (desc.texture == nullptr || !desc.texture->valid() ||
            desc.texture->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier resource is invalid or foreign");
        }
        if (desc.sourceStages == PipelineStage::none ||
            desc.destinationStages == PipelineStage::none ||
            desc.newLayout == TextureLayout::undefined ||
            (desc.transferOwnership &&
             desc.sourceQueue == desc.destinationQueue)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "texture barrier state is invalid");
        }
        auto resource = state_->runtime->resolve<detail::TexturePayload>(
            detail::ObjectKind::texture, desc.texture->state_->handle);
        if (!resource) {
            return detail::invalid_object("texture barrier resource");
        }
        desc.texture = nullptr;
        operation.barrier.textures.push_back(
            {.desc = desc, .resource = std::move(resource)});
    }
    const auto resolve_alias_buffer = [&](Buffer* buffer) {
        return buffer != nullptr && buffer->valid() &&
                       buffer->state_->runtime.get() == state_->runtime.get()
                   ? state_->runtime->resolve<detail::BufferPayload>(
                         detail::ObjectKind::buffer, buffer->state_->handle)
                   : std::shared_ptr<detail::BufferPayload>{};
    };
    const auto resolve_alias_texture = [&](Texture* texture) {
        return texture != nullptr && texture->valid() &&
                       texture->state_->runtime.get() == state_->runtime.get()
                   ? state_->runtime->resolve<detail::TexturePayload>(
                         detail::ObjectKind::texture, texture->state_->handle)
                   : std::shared_ptr<detail::TexturePayload>{};
    };
    for (auto desc : batch.aliasing) {
        const auto beforeCount = static_cast<unsigned>(desc.beforeBuffer != nullptr) +
                                 static_cast<unsigned>(desc.beforeTexture != nullptr);
        const auto afterCount = static_cast<unsigned>(desc.afterBuffer != nullptr) +
                                static_cast<unsigned>(desc.afterTexture != nullptr);
        if (beforeCount != 1 || afterCount != 1) {
            return Status::failure(
                StatusCode::invalid_argument,
                "aliasing barriers require exactly one before and after resource");
        }
        detail::CommandListPayload::AliasingBarrierCommand alias;
        alias.beforeBuffer = resolve_alias_buffer(desc.beforeBuffer);
        alias.beforeTexture = resolve_alias_texture(desc.beforeTexture);
        alias.afterBuffer = resolve_alias_buffer(desc.afterBuffer);
        alias.afterTexture = resolve_alias_texture(desc.afterTexture);
        if ((!alias.beforeBuffer && !alias.beforeTexture) ||
            (!alias.afterBuffer && !alias.afterTexture)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "aliasing barrier resource is invalid or foreign");
        }
        if (desc.sourceStages == PipelineStage::none ||
            desc.destinationStages == PipelineStage::none ||
            (alias.beforeBuffer && alias.beforeBuffer == alias.afterBuffer) ||
            (alias.beforeTexture &&
             alias.beforeTexture == alias.afterTexture)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "aliasing barrier state is invalid");
        }
        desc.beforeBuffer = nullptr;
        desc.beforeTexture = nullptr;
        desc.afterBuffer = nullptr;
        desc.afterTexture = nullptr;
        alias.desc = desc;
        operation.barrier.aliasing.push_back(std::move(alias));
    }
    value->operations.push_back(std::move(operation));
    return Status::success();
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
        auto pipeline = state_->runtime->resolve<detail::PipelinePayload>(
            detail::ObjectKind::pipeline, object.value);
        if (!pipeline || value->activeEncoder != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "graphics pipeline is invalid for encoder");
        }
        if (pipeline->desc.colorTargets.size() !=
                value->renderColorFormats.size() ||
            pipeline->desc.multisample.sampleCount != value->renderSampleCount ||
            pipeline->desc.depthStencil.format !=
                value->renderDepthStencilFormat) {
            return Status::failure(
                StatusCode::invalid_argument,
                "graphics pipeline attachments do not match the render pass");
        }
        for (std::size_t index = 0; index < value->renderColorFormats.size();
             ++index) {
            if (pipeline->desc.colorTargets[index].format !=
                value->renderColorFormats[index]) {
                return Status::failure(
                    StatusCode::invalid_argument,
                    "graphics pipeline color format does not match render pass");
            }
        }
        value->retained.push_back(pipeline);
        value->graphicsPipelineBound = true;
        value->graphicsPipeline = pipeline;
        value->viewportSet = !has_dynamic_state(
            pipeline->desc.dynamicState, DynamicState::viewport);
        value->scissorSet = !has_dynamic_state(
            pipeline->desc.dynamicState, DynamicState::scissor);
        value->blendConstantSet = !has_dynamic_state(
            pipeline->desc.dynamicState, DynamicState::blend_constant);
        value->stencilReferenceSet = !has_dynamic_state(
            pipeline->desc.dynamicState, DynamicState::stencil_reference);
        value->depthBiasSet = !has_dynamic_state(
            pipeline->desc.dynamicState, DynamicState::depth_bias);
        detail::NativeCommand command;
        command.kind = detail::NativeCommandKind::bind_graphics_pipeline;
        command.object = pipeline->native;
        detail::record_native(*value, std::move(command));
        break;
    }
    case 2:
    case 3:
    case 5:
    case 7:
    case 8: {
        auto buffer = state_->runtime->resolve<detail::BufferPayload>(
            detail::ObjectKind::buffer, object.value);
        if (!buffer) {
            return detail::invalid_object("buffer");
        }
        detail::NativeCommand command;
        command.object = buffer->native;
        if (opcode == 2) {
            if (arg1 >= buffer->desc.size || value->activeEncoder != 1 ||
                !has_usage(buffer->desc.usage, BufferUsage::vertex)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "vertex buffer usage is invalid");
            }
            command.kind = detail::NativeCommandKind::bind_vertex_buffer;
            command.arguments[0] = arg0;
            command.arguments[1] = arg1;
        } else if (opcode == 3) {
            if (arg1 >= buffer->desc.size || value->activeEncoder != 1 ||
                !has_usage(buffer->desc.usage, BufferUsage::uniform)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "uniform buffer usage is invalid");
            }
            command.kind = detail::NativeCommandKind::bind_uniform_buffer;
            command.arguments[0] = arg0;
            command.arguments[1] = arg1;
        } else if (opcode == 5 && value->activeEncoder == 1) {
            if (arg0 >= buffer->desc.size ||
                !has_usage(buffer->desc.usage, BufferUsage::index)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "index buffer usage is invalid");
            }
            command.kind = detail::NativeCommandKind::bind_index_buffer;
            command.arguments[0] = arg0;
            command.arguments[1] = arg1;
            value->indexBufferBound = true;
        } else if (opcode == 5 && value->activeEncoder == 2) {
            if (arg1 >= buffer->desc.size ||
                !has_usage(buffer->desc.usage, BufferUsage::storage)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "storage buffer usage is invalid");
            }
            command.kind = detail::NativeCommandKind::bind_storage_buffer;
            command.arguments[0] = arg0;
            command.arguments[1] = arg1;
        } else {
            return Status::failure(StatusCode::invalid_argument,
                                   "buffer command is invalid for encoder");
        }
        value->retained.push_back(buffer);
        detail::record_native(*value, std::move(command));
        break;
    }
    case 4: {
        auto pipeline = state_->runtime->resolve<detail::ComputePipelinePayload>(
            detail::ObjectKind::compute_pipeline, object.value);
        if (!pipeline || value->activeEncoder != 2) {
            return Status::failure(StatusCode::invalid_argument,
                                   "compute pipeline is invalid for encoder");
        }
        value->retained.push_back(pipeline);
        value->computePipelineBound = true;
        value->computePipeline = pipeline;
        detail::NativeCommand command;
        command.kind = detail::NativeCommandKind::bind_compute_pipeline;
        command.object = pipeline->native;
        command.arguments = {pipeline->requiredWorkgroupSize.width,
                             pipeline->requiredWorkgroupSize.height,
                             pipeline->requiredWorkgroupSize.depth,
                             pipeline->preferredWorkgroupSize.width,
                             pipeline->preferredWorkgroupSize.height,
                             pipeline->preferredWorkgroupSize.depth};
        detail::record_native(*value, std::move(command));
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
        {
            detail::NativeCommand command;
            command.kind = detail::NativeCommandKind::draw;
            command.arguments = {arg0, arg1};
            detail::record_native(*value, std::move(command));
        }
        break;
    case 9:
        if (value->activeEncoder != 2 || !value->computePipelineBound ||
            arg0 == 0 || arg1 == 0) {
            return Status::failure(StatusCode::invalid_state,
                                   "dispatch requires a pipeline and non-zero groups");
        }
        state_->runtime->update_stats(
            [](BackendStats& stats) { ++stats.dispatchesRecorded; });
        {
            detail::NativeCommand command;
            command.kind = detail::NativeCommandKind::dispatch;
            command.arguments = {arg0, arg1, 1};
            detail::record_native(*value, std::move(command));
        }
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
    if (encoderKind == 1) {
        detail::NativeCommand command;
        command.kind = detail::NativeCommandKind::end_render;
        detail::record_native(*value, std::move(command));
    } else if (encoderKind == 2) {
        detail::NativeCommand command;
        command.kind = detail::NativeCommandKind::end_compute;
        detail::record_native(*value, std::move(command));
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

namespace detail {

[[nodiscard]] Status record_bind_group(
    ObjectState& state, std::uint32_t encoderKind, std::uint32_t group,
    Handle bindGroupHandle, std::span<const std::uint32_t> dynamicOffsets) {
    const auto list = state.runtime->resolve<CommandListPayload>(
        ObjectKind::command_list, state.handle);
    const auto bindGroup = state.runtime->resolve<BindGroupPayload>(
        ObjectKind::bind_group, bindGroupHandle);
    if (!list || !bindGroup || !bindGroup->arena || !bindGroup->layout) {
        return invalid_object("bind group");
    }
    std::lock_guard listLock{list->mutex};
    if (list->owner != std::this_thread::get_id() ||
        list->state != CommandListState::recording ||
        list->activeEncoder != encoderKind) {
        return Status::failure(StatusCode::invalid_state,
                               "bind group requires an active owned encoder");
    }
    {
        std::lock_guard arenaLock{bindGroup->arena->mutex};
        if (bindGroup->arenaEpoch != bindGroup->arena->epoch) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind group was retired by an arena reset");
        }
    }
    const auto pipelineLayout =
        encoderKind == 1 && list->graphicsPipeline
            ? list->graphicsPipeline->layout
            : encoderKind == 2 && list->computePipeline
                  ? list->computePipeline->layout
                  : std::shared_ptr<PipelineLayoutPayload>{};
    if (!pipelineLayout || group != bindGroup->layout->desc.group) {
        return Status::failure(StatusCode::invalid_state,
                               "bind group requires a compatible bound pipeline");
    }
    const auto expectedLayout = std::find_if(
        pipelineLayout->layoutHandles.begin(), pipelineLayout->layoutHandles.end(),
        [&](Handle handle) { return handle == bindGroup->layoutHandle; });
    if (expectedLayout == pipelineLayout->layoutHandles.end()) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group layout does not match pipeline layout");
    }

    std::vector<const BoundResource*> ordered;
    ordered.reserve(bindGroup->resources.size());
    for (const auto& resource : bindGroup->resources) {
        ordered.push_back(&resource);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return std::pair{lhs->entry.binding, lhs->entry.arrayElement} <
               std::pair{rhs->entry.binding, rhs->entry.arrayElement};
    });
    std::size_t expectedDynamicOffsets = 0;
    for (const auto* resource : ordered) {
        const auto* layoutEntry =
            find_layout_entry(*bindGroup->layout, resource->entry.binding);
        if (layoutEntry != nullptr && layoutEntry->dynamicOffset) {
            ++expectedDynamicOffsets;
        }
    }
    if (dynamicOffsets.size() != expectedDynamicOffsets) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group dynamic-offset count is invalid");
    }

    NativeCommand command;
    command.kind = NativeCommandKind::bind_group;
    command.arguments[0] = group;
    command.bindings.reserve(bindGroup->resources.size() +
                             bindGroup->layout->immutableSamplers.size());
    std::size_t dynamicIndex = 0;
    for (const auto* resource : ordered) {
        const auto* layoutEntry =
            find_layout_entry(*bindGroup->layout, resource->entry.binding);
        if (layoutEntry == nullptr) {
            return Status::failure(StatusCode::invalid_state,
                                   "bind group layout entry disappeared");
        }
        std::size_t effectiveOffset = resource->entry.offset;
        if (layoutEntry->dynamicOffset) {
            const auto dynamicOffset = dynamicOffsets[dynamicIndex++];
            const auto alignment =
                layoutEntry->type == BindingType::uniform_buffer
                    ? state.runtime->config.bindingCapabilities
                          .minUniformBufferOffsetAlignment
                    : state.runtime->config.bindingCapabilities
                          .minStorageBufferOffsetAlignment;
            if (alignment == 0 || dynamicOffset % alignment != 0 ||
                effectiveOffset >
                    std::numeric_limits<std::size_t>::max() - dynamicOffset) {
                return Status::failure(StatusCode::invalid_argument,
                                       "dynamic buffer offset is misaligned");
            }
            effectiveOffset += dynamicOffset;
            if (!resource->buffer ||
                !buffer_range_valid(*resource->buffer, effectiveOffset,
                                    resource->entry.size)) {
                return Status::failure(StatusCode::invalid_argument,
                                       "dynamic buffer offset exceeds allocation");
            }
        }
        NativeBindingResource native;
        native.group = group;
        native.binding = resource->entry.binding;
        native.arrayElement = resource->entry.arrayElement;
        native.type = resource->type;
        native.visibility = layoutEntry->visibility;
        native.offset = effectiveOffset;
        native.size = resource->entry.size;
        if (resource->buffer) {
            native.resource = resource->buffer->native;
        } else if (resource->textureView) {
            native.resource = resource->textureView->native;
        } else if (resource->sampler) {
            native.resource = resource->sampler->native;
        }
        command.bindings.push_back(std::move(native));
    }
    for (const auto& [binding, sampler] :
         bindGroup->layout->immutableSamplers) {
        const auto* entry = find_layout_entry(*bindGroup->layout, binding);
        if (entry == nullptr || !sampler) {
            return Status::failure(StatusCode::invalid_state,
                                   "immutable sampler mapping is incomplete");
        }
        NativeBindingResource native;
        native.group = group;
        native.binding = binding;
        native.type = BindingType::sampler;
        native.visibility = entry->visibility;
        native.resource = sampler->native;
        command.bindings.push_back(std::move(native));
    }
    list->retained.push_back(bindGroup);
    detail::record_native(*list, std::move(command));
    return Status::success();
}

[[nodiscard]] bool push_constant_range_covers(
    const PipelineLayoutPayload& layout, ShaderStageMask stages,
    std::uint32_t offset, std::size_t size) {
    const auto end = static_cast<std::uint64_t>(offset) + size;
    for (const auto stage : {ShaderStage::vertex, ShaderStage::fragment,
                             ShaderStage::compute}) {
        const auto mask = shader_stage_mask(stage);
        if (!has_stage(stages, mask)) {
            continue;
        }
        const auto found = std::find_if(
            layout.desc.pushConstants.begin(), layout.desc.pushConstants.end(),
            [&](const PushConstantRange& range) {
                return range.stage == stage && range.offset <= offset &&
                       end <= static_cast<std::uint64_t>(range.offset) +
                                  range.size;
            });
        if (found == layout.desc.pushConstants.end()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Status record_push_constants(
    ObjectState& state, std::uint32_t encoderKind, ShaderStageMask stages,
    std::uint32_t offset, std::span<const std::byte> data) {
    const auto list = state.runtime->resolve<CommandListPayload>(
        ObjectKind::command_list, state.handle);
    if (!list) {
        return invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() ||
        list->state != CommandListState::recording ||
        list->activeEncoder != encoderKind || data.empty() ||
        stages == ShaderStageMask::none || offset % 4 != 0 ||
        data.size() % 4 != 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "push-constant command is invalid");
    }
    const auto layout =
        encoderKind == 1 && list->graphicsPipeline
            ? list->graphicsPipeline->layout
            : encoderKind == 2 && list->computePipeline
                  ? list->computePipeline->layout
                  : std::shared_ptr<PipelineLayoutPayload>{};
    if (!layout || !push_constant_range_covers(*layout, stages, offset,
                                               data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "push constants exceed pipeline layout ranges");
    }
    NativeCommand command;
    command.kind = NativeCommandKind::push_constants;
    command.arguments[0] = static_cast<std::uint32_t>(stages);
    command.arguments[1] = offset;
    command.bytes.assign(data.begin(), data.end());
    detail::record_native(*list, std::move(command));
    return Status::success();
}

[[nodiscard]] Status record_draw(ObjectState& state, NativeCommand command,
                                 bool indexed) {
    const auto list = state.runtime->resolve<CommandListPayload>(
        ObjectKind::command_list, state.handle);
    if (!list) {
        return invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() ||
        list->state != CommandListState::recording || list->activeEncoder != 1 ||
        !list->graphicsPipelineBound ||
        (indexed && !list->indexBufferBound) || command.arguments[0] == 0 ||
        command.arguments[1] == 0 || !list->viewportSet || !list->scissorSet ||
        !list->blendConstantSet || !list->stencilReferenceSet ||
        !list->depthBiasSet) {
        return Status::failure(StatusCode::invalid_state,
                               "draw requires complete bound state and non-zero counts");
    }
    detail::record_native(*list, std::move(command));
    state.runtime->update_stats(
        [](BackendStats& stats) { ++stats.drawsRecorded; });
    return Status::success();
}

[[nodiscard]] Status record_dispatch(ObjectState& state,
                                     NativeCommand command) {
    const auto list = state.runtime->resolve<CommandListPayload>(
        ObjectKind::command_list, state.handle);
    if (!list) {
        return invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() ||
        list->state != CommandListState::recording || list->activeEncoder != 2 ||
        !list->computePipelineBound || command.arguments[0] == 0 ||
        command.arguments[1] == 0 || command.arguments[2] == 0) {
        return Status::failure(
            StatusCode::invalid_state,
            "dispatch requires a bound pipeline and non-zero group counts");
    }
    detail::record_native(*list, std::move(command));
    state.runtime->update_stats(
        [](BackendStats& stats) { ++stats.dispatchesRecorded; });
    return Status::success();
}

} // namespace detail

Status RenderEncoder::bind_group(
    std::uint32_t group, BindGroup& bindGroup,
    std::span<const std::uint32_t> dynamicOffsets) {
    if (!active_ || list_ == nullptr || !bindGroup.valid() ||
        bindGroup.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group belongs to another runtime or is stale");
    }
    return detail::record_bind_group(*list_->state_, 1, group,
                                     bindGroup.state_->handle, dynamicOffsets);
}

Status RenderEncoder::push_constants(ShaderStageMask stages,
                                     std::uint32_t offset,
                                     std::span<const std::byte> data) {
    if (!active_ || list_ == nullptr ||
        has_stage(stages, ShaderStageMask::compute)) {
        return Status::failure(StatusCode::invalid_argument,
                               "render push-constant stages are invalid");
    }
    return detail::record_push_constants(*list_->state_, 1, stages, offset,
                                         data);
}

Status RenderEncoder::set_viewports(std::uint32_t first,
                                    std::span<const Viewport> viewports) {
    if (!active_ || list_ == nullptr || viewports.empty()) {
        return detail::invalid_object("render encoder");
    }
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!list) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipeline ||
        !has_dynamic_state(list->graphicsPipeline->desc.dynamicState,
                           DynamicState::viewport) ||
        static_cast<std::uint64_t>(first) + viewports.size() >
            list_->state_->runtime->config.pipelineCapabilities.maxViewports) {
        return Status::failure(StatusCode::invalid_state,
                               "dynamic viewports are unavailable for this pipeline");
    }
    for (const auto& viewport : viewports) {
        if (viewport.width <= 0.0F || viewport.height <= 0.0F ||
            viewport.minimumDepth < 0.0F || viewport.maximumDepth > 1.0F ||
            viewport.minimumDepth > viewport.maximumDepth) {
            return Status::failure(StatusCode::invalid_argument,
                                   "viewport dimensions or depth range are invalid");
        }
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::set_viewports;
    command.arguments[0] = first;
    command.viewports.assign(viewports.begin(), viewports.end());
    detail::record_native(*list, std::move(command));
    list->viewportSet = list->viewportSet || first == 0;
    return Status::success();
}

Status RenderEncoder::set_scissors(std::uint32_t first,
                                   std::span<const ScissorRect> scissors) {
    if (!active_ || list_ == nullptr || scissors.empty()) {
        return detail::invalid_object("render encoder");
    }
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!list) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipeline ||
        !has_dynamic_state(list->graphicsPipeline->desc.dynamicState,
                           DynamicState::scissor) ||
        static_cast<std::uint64_t>(first) + scissors.size() >
            list_->state_->runtime->config.pipelineCapabilities.maxViewports) {
        return Status::failure(StatusCode::invalid_state,
                               "dynamic scissors are unavailable for this pipeline");
    }
    for (const auto& scissor : scissors) {
        if (scissor.x < 0 || scissor.y < 0 || scissor.width == 0 ||
            scissor.height == 0) {
            return Status::failure(StatusCode::invalid_argument,
                                   "scissor rectangle is invalid");
        }
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::set_scissors;
    command.arguments[0] = first;
    command.scissors.assign(scissors.begin(), scissors.end());
    detail::record_native(*list, std::move(command));
    list->scissorSet = list->scissorSet || first == 0;
    return Status::success();
}

Status RenderEncoder::set_blend_constant(
    const std::array<float, 4>& color) {
    if (!active_ || list_ == nullptr ||
        !std::all_of(color.begin(), color.end(),
                     [](float component) { return std::isfinite(component); })) {
        return Status::failure(StatusCode::invalid_argument,
                               "blend constant is invalid");
    }
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!list) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipeline ||
        !has_dynamic_state(list->graphicsPipeline->desc.dynamicState,
                           DynamicState::blend_constant)) {
        return Status::failure(StatusCode::invalid_state,
                               "dynamic blend constant is unavailable");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::set_blend_constant;
    const auto bytes = std::as_bytes(std::span{color});
    command.bytes.assign(bytes.begin(), bytes.end());
    detail::record_native(*list, std::move(command));
    list->blendConstantSet = true;
    return Status::success();
}

Status RenderEncoder::set_stencil_reference(std::uint32_t reference) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!list) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipeline ||
        !has_dynamic_state(list->graphicsPipeline->desc.dynamicState,
                           DynamicState::stencil_reference)) {
        return Status::failure(StatusCode::invalid_state,
                               "dynamic stencil reference is unavailable");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::set_stencil_reference;
    command.arguments = {reference};
    detail::record_native(*list, std::move(command));
    list->stencilReferenceSet = true;
    return Status::success();
}

Status RenderEncoder::set_depth_bias(float constantFactor, float slopeScale,
                                     float clamp) {
    if (!active_ || list_ == nullptr || !std::isfinite(constantFactor) ||
        !std::isfinite(slopeScale) || !std::isfinite(clamp)) {
        return Status::failure(StatusCode::invalid_argument,
                               "depth bias is invalid");
    }
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!list) {
        return detail::invalid_object("command list");
    }
    std::lock_guard lock{list->mutex};
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipeline ||
        !has_dynamic_state(list->graphicsPipeline->desc.dynamicState,
                           DynamicState::depth_bias)) {
        return Status::failure(StatusCode::invalid_state,
                               "dynamic depth bias is unavailable");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::set_depth_bias;
    const std::array<float, 3> values{constantFactor, slopeScale, clamp};
    const auto bytes = std::as_bytes(std::span{values});
    command.bytes.assign(bytes.begin(), bytes.end());
    detail::record_native(*list, std::move(command));
    list->depthBiasSet = true;
    return Status::success();
}

Status RenderEncoder::draw(std::uint32_t vertexCount,
                           std::uint32_t instanceCount,
                           std::uint32_t firstVertex,
                           std::uint32_t firstInstance) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::draw;
    command.arguments = {vertexCount, instanceCount, firstVertex, firstInstance};
    return detail::record_draw(*list_->state_, std::move(command), false);
}

Status RenderEncoder::draw_indexed(std::uint32_t indexCount,
                                   std::uint32_t instanceCount,
                                   std::uint32_t firstIndex,
                                   std::int32_t vertexOffset,
                                   std::uint32_t firstInstance) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("render encoder");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::draw_indexed;
    command.arguments = {indexCount, instanceCount, firstIndex,
                         static_cast<std::uint32_t>(vertexOffset), firstInstance};
    return detail::record_draw(*list_->state_, std::move(command), true);
}

Status RenderEncoder::draw_indirect(Buffer& buffer, std::size_t offset,
                                    bool indexed, std::uint32_t drawCount,
                                    std::uint32_t stride) {
    if (!active_ || list_ == nullptr || !buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "indirect buffer belongs to another runtime");
    }
    const auto indirect = detail::payload<detail::BufferPayload>(
        buffer.state_, detail::ObjectKind::buffer);
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!indirect || !list) {
        return detail::invalid_object("indirect draw resource");
    }
    const auto commandSize = indexed ? std::size_t{20} : std::size_t{16};
    const auto effectiveStride = stride == 0 ? commandSize : stride;
    std::lock_guard lock{list->mutex};
    if (!list_->state_->runtime->config.pipelineCapabilities.indirect) {
        return detail::unsupported(*list_->state_->runtime, "indirect drawing");
    }
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipelineBound || (indexed && !list->indexBufferBound) ||
        !list->viewportSet || !list->scissorSet || !list->blendConstantSet ||
        !list->stencilReferenceSet || !list->depthBiasSet ||
        !has_usage(indirect->desc.usage, BufferUsage::indirect) ||
        drawCount == 0 || offset % 4 != 0 || effectiveStride < commandSize ||
        effectiveStride % 4 != 0 || offset > indirect->desc.size ||
        commandSize > indirect->desc.size - offset ||
        static_cast<std::size_t>(drawCount - 1) >
            (indirect->desc.size - offset - commandSize) / effectiveStride) {
        return Status::failure(StatusCode::invalid_argument,
                               "indirect draw range or state is invalid");
    }
    list->retained.push_back(indirect);
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::draw_indirect;
    command.object = indirect->native;
    command.arguments = {offset, indexed ? 1u : 0u, drawCount,
                         effectiveStride};
    detail::record_native(*list, std::move(command));
    list_->state_->runtime->update_stats(
        [](BackendStats& stats) { ++stats.drawsRecorded; });
    return Status::success();
}

Status RenderEncoder::draw_indirect_count(
    Buffer& buffer, std::size_t offset, Buffer& countBuffer,
    std::size_t countOffset, std::uint32_t maximumDrawCount,
    std::uint32_t stride, bool indexed) {
    if (!active_ || list_ == nullptr || !buffer.valid() ||
        !countBuffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get() ||
        countBuffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "indirect-count buffers belong to another runtime");
    }
    const auto indirect = detail::payload<detail::BufferPayload>(
        buffer.state_, detail::ObjectKind::buffer);
    const auto count = detail::payload<detail::BufferPayload>(
        countBuffer.state_, detail::ObjectKind::buffer);
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!indirect || !count || !list) {
        return detail::invalid_object("indirect-count draw resource");
    }
    const auto commandSize = indexed ? std::size_t{20} : std::size_t{16};
    const auto effectiveStride = stride == 0 ? commandSize : stride;
    std::lock_guard lock{list->mutex};
    if (!list_->state_->runtime->config.pipelineCapabilities.indirectCount) {
        return detail::unsupported(*list_->state_->runtime,
                                   "indirect-count drawing");
    }
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 1 ||
        !list->graphicsPipelineBound || (indexed && !list->indexBufferBound) ||
        !list->viewportSet || !list->scissorSet || !list->blendConstantSet ||
        !list->stencilReferenceSet || !list->depthBiasSet ||
        !has_usage(indirect->desc.usage, BufferUsage::indirect) ||
        !has_usage(count->desc.usage, BufferUsage::indirect) ||
        maximumDrawCount == 0 || offset % 4 != 0 || countOffset % 4 != 0 ||
        effectiveStride < commandSize || effectiveStride % 4 != 0 ||
        !detail::buffer_range_valid(*count, countOffset, sizeof(std::uint32_t)) ||
        offset > indirect->desc.size || commandSize > indirect->desc.size - offset ||
        static_cast<std::size_t>(maximumDrawCount - 1) >
            (indirect->desc.size - offset - commandSize) / effectiveStride) {
        return Status::failure(StatusCode::invalid_argument,
                               "indirect-count draw range or state is invalid");
    }
    list->retained.push_back(indirect);
    list->retained.push_back(count);
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::draw_indirect_count;
    command.object = indirect->native;
    command.secondaryObject = count->native;
    command.arguments = {offset, countOffset, maximumDrawCount, effectiveStride,
                         indexed ? 1u : 0u};
    detail::record_native(*list, std::move(command));
    list_->state_->runtime->update_stats(
        [](BackendStats& stats) { ++stats.drawsRecorded; });
    return Status::success();
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

Status ComputeEncoder::bind_group(
    std::uint32_t group, BindGroup& bindGroup,
    std::span<const std::uint32_t> dynamicOffsets) {
    if (!active_ || list_ == nullptr || !bindGroup.valid() ||
        bindGroup.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "bind group belongs to another runtime or is stale");
    }
    return detail::record_bind_group(*list_->state_, 2, group,
                                     bindGroup.state_->handle, dynamicOffsets);
}

Status ComputeEncoder::push_constants(std::uint32_t offset,
                                      std::span<const std::byte> data) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    return detail::record_push_constants(*list_->state_, 2,
                                         ShaderStageMask::compute, offset,
                                         data);
}

Status ComputeEncoder::dispatch(std::uint32_t x, std::uint32_t y,
                                std::uint32_t z) {
    if (!active_ || list_ == nullptr) {
        return detail::invalid_object("compute encoder");
    }
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::dispatch;
    command.arguments = {x, y, z};
    return detail::record_dispatch(*list_->state_, std::move(command));
}

Status ComputeEncoder::dispatch_indirect(Buffer& buffer, std::size_t offset) {
    if (!active_ || list_ == nullptr || !buffer.valid() ||
        buffer.state_->runtime.get() != list_->state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "dispatch-indirect buffer belongs to another runtime");
    }
    const auto indirect = detail::payload<detail::BufferPayload>(
        buffer.state_, detail::ObjectKind::buffer);
    const auto list = detail::payload<detail::CommandListPayload>(
        list_->state_, detail::ObjectKind::command_list);
    if (!indirect || !list) {
        return detail::invalid_object("dispatch-indirect resource");
    }
    std::lock_guard lock{list->mutex};
    if (!list_->state_->runtime->config.pipelineCapabilities.indirect) {
        return detail::unsupported(*list_->state_->runtime,
                                   "indirect dispatch");
    }
    if (list->owner != std::this_thread::get_id() || list->activeEncoder != 2 ||
        !list->computePipelineBound ||
        !has_usage(indirect->desc.usage, BufferUsage::indirect) ||
        offset % 4 != 0 ||
        !detail::buffer_range_valid(*indirect, offset,
                                    3 * sizeof(std::uint32_t))) {
        return Status::failure(StatusCode::invalid_argument,
                               "dispatch-indirect range or state is invalid");
    }
    list->retained.push_back(indirect);
    detail::NativeCommand command;
    command.kind = detail::NativeCommandKind::dispatch_indirect;
    command.object = indirect->native;
    command.arguments = {offset};
    detail::record_native(*list, std::move(command));
    list_->state_->runtime->update_stats(
        [](BackendStats& stats) { ++stats.dispatchesRecorded; });
    return Status::success();
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

Status Queue::submit(const QueueSubmitDesc& desc) {
    if (!valid() || desc.commandLists.empty()) {
        return Status::failure(StatusCode::invalid_argument,
                               "submit requires a valid queue and command lists");
    }
    std::vector<detail::Handle> handles;
    handles.reserve(desc.commandLists.size());
    for (auto* list : desc.commandLists) {
        if (list == nullptr || !list->valid() ||
            list->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submitted command list is invalid or foreign");
        }
        if (std::find(handles.begin(), handles.end(), list->state_->handle) !=
            handles.end()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "a command list cannot appear twice in one submission");
        }
        handles.push_back(list->state_->handle);
    }
    std::vector<detail::SemaphorePointHandle> waits;
    std::vector<detail::SemaphorePointHandle> signals;
    waits.reserve(desc.waits.size());
    signals.reserve(desc.signals.size());
    for (const auto& wait : desc.waits) {
        if (wait.semaphore == nullptr || !wait.semaphore->valid() ||
            wait.semaphore->state_->runtime.get() != state_->runtime.get() ||
            wait.stages == PipelineStage::none) {
            return Status::failure(StatusCode::invalid_argument,
                                   "submit wait semaphore is invalid or foreign");
        }
        waits.push_back(
            {wait.semaphore->state_->handle, wait.value, wait.stages});
    }
    for (const auto& signal : desc.signals) {
        if (signal.semaphore == nullptr || !signal.semaphore->valid() ||
            signal.semaphore->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(
                StatusCode::invalid_argument,
                "submit signal semaphore is invalid or foreign");
        }
        signals.push_back({signal.semaphore->state_->handle, signal.value,
                           PipelineStage::bottom});
    }
    detail::Handle fenceHandle = 0;
    if (desc.signalFence != nullptr) {
        if (!desc.signalFence->valid() ||
            desc.signalFence->state_->runtime.get() != state_->runtime.get()) {
            return Status::failure(StatusCode::invalid_argument,
                                   "signal fence is invalid or foreign");
        }
        fenceHandle = desc.signalFence->state_->handle;
    }
    return state_->runtime->dispatch->submit(*state_->runtime, state_->handle,
                                             handles, waits, signals, fenceHandle,
                                             desc.signalFenceValue,
                                             desc.waitTimeout);
}

Status Queue::submit(std::span<CommandList* const> commandLists,
                     Fence* signalFence, std::uint64_t signalValue) {
    QueueSubmitDesc desc;
    desc.commandLists = commandLists;
    desc.signalFence = signalFence;
    desc.signalFenceValue = signalValue;
    return submit(desc);
}

Status Queue::present(const QueuePresentDesc& desc) {
    if (!valid() || desc.swapchain == nullptr || !desc.swapchain->valid() ||
        desc.swapchain->state_->object->runtime.get() != state_->runtime.get()) {
        return Status::failure(StatusCode::invalid_argument,
                               "present objects are invalid or foreign");
    }
    std::vector<detail::SemaphorePointHandle> waits;
    waits.reserve(desc.waits.size());
    for (const auto& wait : desc.waits) {
        if (wait.semaphore == nullptr || !wait.semaphore->valid() ||
            wait.semaphore->state_->runtime.get() != state_->runtime.get() ||
            wait.stages == PipelineStage::none) {
            return Status::failure(
                StatusCode::invalid_argument,
                "present wait semaphore is invalid or foreign");
        }
        waits.push_back(
            {wait.semaphore->state_->handle, wait.value, wait.stages});
    }
    return state_->runtime->dispatch->present(
        *state_->runtime, state_->handle,
        desc.swapchain->state_->object->handle, desc.imageIndex, waits);
}

Status Queue::present(Swapchain& swapchain, std::uint32_t imageIndex) {
    QueuePresentDesc desc;
    desc.swapchain = &swapchain;
    desc.imageIndex = imageIndex;
    return present(desc);
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
    const auto completed = [&] { return value->value >= target; };
    if (timeout == std::chrono::nanoseconds::max()) {
        value->changed.wait(lock, completed);
        return Status::success();
    }
    if (value->changed.wait_for(lock, timeout, completed)) {
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
    if (!runtime.config.logicalResources) {
        if (runtime.config.acquireSwapchain == nullptr) {
            return {.status = detail::unsupported(runtime,
                                                  "swapchain acquisition")};
        }
        std::shared_ptr<void> nativeSemaphore;
        if (runtime.config.createSemaphore != nullptr) {
            auto semaphoreResult = runtime.config.createSemaphore(
                SemaphoreDesc{.initialValue = 1,
                              .debugName = value->desc.debugName +
                                           " acquire"});
            if (!semaphoreResult.ok()) {
                return {.status = semaphoreResult.status()};
            }
            nativeSemaphore = std::move(semaphoreResult).value();
        }
        auto acquired = runtime.config.acquireSwapchain(value->native);
        if (!acquired.ok()) {
            if (acquired.status().code == StatusCode::device_lost &&
                value->device) {
                value->device->lost.store(true);
            }
            return {.status = acquired.status()};
        }
        auto nativeImage = std::move(acquired).value();
        if (!nativeImage.status.ok() &&
            nativeImage.status.code != StatusCode::suboptimal) {
            if (nativeImage.status.code == StatusCode::device_lost &&
                value->device) {
                value->device->lost.store(true);
            }
            return {.status = std::move(nativeImage.status)};
        }
        const TextureDesc imageDesc{
            .extent = {nativeImage.extent.width != 0
                           ? nativeImage.extent.width
                           : value->desc.extent.width,
                       nativeImage.extent.height != 0
                           ? nativeImage.extent.height
                           : value->desc.extent.height,
                       1},
            .format = value->desc.format,
            .usage = TextureUsage::color_attachment | TextureUsage::present,
            .debugName = value->desc.debugName + " image",
        };
        value->image = std::make_shared<detail::TexturePayload>(
            imageDesc, nullptr, std::move(nativeImage.texture), false);
        for (auto& sync : value->image->sync) {
            sync.layout = TextureLayout::present;
            sync.owner = QueueKind::graphics;
            sync.ownerSet = true;
        }
        const auto imageHandle = runtime.allocate(detail::ObjectKind::texture,
                                                  value->image);
        state_->image = std::make_unique<Texture>(
            detail::Factory::texture(state_->object->runtime, imageHandle));
        const auto semaphoreHandle = runtime.allocate(
            detail::ObjectKind::semaphore,
            std::make_shared<detail::SemaphorePayload>(
                1, std::move(nativeSemaphore)));
        state_->available = std::make_unique<Semaphore>(
            detail::Factory::semaphore(state_->object->runtime,
                                       semaphoreHandle));
        value->acquired = true;
        return {
            .image = state_->image.get(),
            .imageIndex = nativeImage.imageIndex,
            .status = std::move(nativeImage.status),
            .available = state_->available.get(),
            .availableValue = 1,
        };
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
    if (!state_->object->runtime->config.logicalResources) {
        if (state_->object->runtime->config.resizeSwapchain == nullptr) {
            return detail::unsupported(*state_->object->runtime,
                                       "swapchain resize");
        }
        if (auto status = state_->object->runtime->config.resizeSwapchain(
                value->native, extent);
            !status.ok()) {
            if (status.code == StatusCode::device_lost && value->device) {
                value->device->lost.store(true);
            }
            return status;
        }
        value->desc.extent = extent;
        value->image.reset();
        state_->image.reset();
        state_->available.reset();
        return Status::success();
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
    for (auto& sync : value->image->sync) {
        sync.layout = TextureLayout::present;
        sync.owner = QueueKind::graphics;
        sync.ownerSet = true;
    }
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

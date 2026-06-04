#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cstddef>
#include <limits>

namespace truffle::rhi::validation {

[[nodiscard]] constexpr bool is_non_zero(Extent2D extent) noexcept {
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] constexpr bool extent_within(Extent2D extent,
                                           std::uint32_t maxDimension) noexcept {
    return is_non_zero(extent) && maxDimension != 0 &&
           extent.width <= maxDimension && extent.height <= maxDimension;
}

[[nodiscard]] constexpr bool frame_count_supported(
    std::uint32_t framesInFlight,
    const Capabilities& capabilities) noexcept {
    return framesInFlight != 0 &&
           framesInFlight <= capabilities.maxFramesInFlight;
}

[[nodiscard]] inline bool present_mode_supported(
    PresentMode presentMode,
    const Capabilities& capabilities) noexcept {
    return supports_present_mode(capabilities, presentMode);
}

[[nodiscard]] constexpr bool native_surface_handles_valid(
    const NativeSurface& native) noexcept {
    switch (native.kind) {
    case NativeSurfaceKind::headless:
        return native.handle == nullptr && native.display == nullptr;
    case NativeSurfaceKind::cocoa_layer:
        return native.handle != nullptr && native.display == nullptr;
    case NativeSurfaceKind::win32:
        return native.handle != nullptr;
    case NativeSurfaceKind::wayland:
    case NativeSurfaceKind::xcb:
        return native.handle != nullptr && native.display != nullptr;
    case NativeSurfaceKind::external:
        return native.handle != nullptr;
    }
    return false;
}

[[nodiscard]] inline bool native_surface_kind_supported(
    NativeSurfaceKind kind,
    const Capabilities& capabilities) noexcept {
    return supports_native_surface_kind(capabilities, kind);
}

[[nodiscard]] inline bool surface_supported(
    const SurfaceDesc& desc,
    const Capabilities& capabilities) noexcept {
    return extent_within(desc.initialExtent,
                         capabilities.limits.maxTextureDimension2D) &&
           native_surface_handles_valid(desc.native) &&
           native_surface_kind_supported(desc.native.kind, capabilities);
}

[[nodiscard]] inline bool swapchain_supported(
    const SwapchainDesc& desc,
    const Capabilities& capabilities) noexcept {
    const auto imageCount = effective_swapchain_image_count(desc);
    return extent_within(desc.extent, capabilities.limits.maxTextureDimension2D) &&
           desc.framesInFlight != 0 &&
           imageCount != 0 &&
           imageCount <= capabilities.maxFramesInFlight &&
           present_mode_supported(desc.presentMode, capabilities);
}

[[nodiscard]] inline bool memory_domain_supported(
    MemoryDomain domain,
    const Capabilities& capabilities) noexcept {
    if (domain == MemoryDomain::automatic) {
        return true;
    }

    for (const auto& heap : capabilities.memoryHeaps) {
        if (heap.kind == MemoryHeapKind::unified) {
            return true;
        }
        if (domain == MemoryDomain::device_local &&
            heap.kind == MemoryHeapKind::device_local) {
            return true;
        }
        if ((domain == MemoryDomain::upload || domain == MemoryDomain::readback) &&
            heap.kind == MemoryHeapKind::host_visible) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1u)) == 0;
}

[[nodiscard]] constexpr bool texture_shape_valid(
    const TextureDesc& desc,
    std::uint32_t maxDimension) noexcept {
    return extent_within(desc.extent, maxDimension) &&
           desc.depth != 0 &&
           desc.depth <= maxDimension &&
           desc.mipLevels != 0 &&
           desc.arrayLayers != 0 &&
           desc.sampleCount != 0 &&
           is_power_of_two(desc.sampleCount) &&
           ((desc.dimension == TextureDimension::one_d &&
             desc.extent.height == 1 && desc.depth == 1) ||
            (desc.dimension == TextureDimension::two_d && desc.depth == 1) ||
            (desc.dimension == TextureDimension::three_d && desc.arrayLayers == 1) ||
            (desc.dimension == TextureDimension::cube &&
             desc.depth == 1 &&
             desc.extent.width == desc.extent.height &&
             desc.arrayLayers % 6u == 0));
}

[[nodiscard]] constexpr bool buffer_supports_usage(
    const BufferDesc& desc,
    BufferUsageFlags usage) noexcept {
    return usage == BufferUsageFlags::none ||
           has_flag(effective_buffer_usage(desc), usage);
}

[[nodiscard]] constexpr bool texture_supports_usage(
    const TextureDesc& desc,
    TextureUsageFlags usage) noexcept {
    return usage == TextureUsageFlags::none ||
           has_flag(effective_texture_usage(desc), usage);
}

[[nodiscard]] constexpr bool buffer_supports_state(
    const BufferDesc& desc,
    ResourceState state) noexcept {
    const auto usage = effective_buffer_usage(desc);
    switch (state) {
        case ResourceState::undefined: return true;
        case ResourceState::copy_source:
            return has_flag(usage, BufferUsageFlags::transfer_source);
        case ResourceState::copy_destination:
            return has_flag(usage, BufferUsageFlags::transfer_destination);
        case ResourceState::shader_read:
            return (usage & (BufferUsageFlags::uniform | BufferUsageFlags::storage)) !=
                   BufferUsageFlags::none;
        case ResourceState::storage_read_write:
            return has_flag(usage, BufferUsageFlags::storage);
        case ResourceState::color_attachment:
        case ResourceState::depth_attachment:
        case ResourceState::present:
            return false;
    }
    return false;
}

[[nodiscard]] constexpr bool texture_supports_state(
    const TextureDesc& desc,
    ResourceState state) noexcept {
    switch (state) {
        case ResourceState::undefined: return true;
        case ResourceState::copy_source:
            return texture_supports_usage(desc, TextureUsageFlags::transfer_source);
        case ResourceState::copy_destination:
            return texture_supports_usage(desc, TextureUsageFlags::transfer_destination);
        case ResourceState::shader_read:
            return texture_supports_usage(desc, TextureUsageFlags::sampled);
        case ResourceState::storage_read_write:
            return texture_supports_usage(desc, TextureUsageFlags::storage);
        case ResourceState::color_attachment:
            return texture_supports_usage(desc, TextureUsageFlags::color_attachment);
        case ResourceState::depth_attachment:
            return texture_supports_usage(desc, TextureUsageFlags::depth_stencil);
        case ResourceState::present:
            return texture_supports_usage(desc, TextureUsageFlags::color_attachment);
    }
    return false;
}

[[nodiscard]] inline bool texture_usage_supported_by_format(
    const Capabilities& capabilities,
    const TextureDesc& desc) noexcept {
    const auto* support = find_format_support(capabilities, desc.format);
    if (!support) {
        return false;
    }

    const auto usage = effective_texture_usage(desc);
    return (!has_flag(usage, TextureUsageFlags::sampled) || support->sampled) &&
           (!has_flag(usage, TextureUsageFlags::color_attachment) ||
            support->colorAttachment) &&
           (!has_flag(usage, TextureUsageFlags::depth_stencil) ||
            support->depthStencilAttachment) &&
           (!has_flag(usage, TextureUsageFlags::storage) ||
            support->storageTexture) &&
           (!has_flag(usage, TextureUsageFlags::transfer_source) ||
            support->transferSource) &&
           (!has_flag(usage, TextureUsageFlags::transfer_destination) ||
            support->transferDestination);
}

[[nodiscard]] constexpr bool align_up(std::size_t value,
                                      std::size_t alignment,
                                      std::size_t& out) noexcept {
    if (!is_power_of_two(alignment)) {
        return false;
    }

    const auto mask = alignment - 1u;
    if (value > std::numeric_limits<std::size_t>::max() - mask) {
        return false;
    }

    out = (value + mask) & ~mask;
    return true;
}

[[nodiscard]] constexpr bool range_fits(std::size_t offset,
                                        std::size_t size,
                                        std::size_t limit) noexcept {
    return size != 0 && offset <= limit && size <= limit - offset;
}

[[nodiscard]] constexpr bool view_range_fits(std::size_t offset,
                                             std::size_t size,
                                             std::size_t limit) noexcept {
    if (size == 0) {
        return offset < limit;
    }
    return range_fits(offset, size, limit);
}

[[nodiscard]] inline bool buffer_view_valid(const BufferViewDesc& view) noexcept {
    if (!view.buffer) {
        return false;
    }

    const auto& desc = view.buffer->desc();
    return view_range_fits(view.offset, view.size, desc.size) &&
           buffer_supports_usage(desc, view.requiredUsage);
}

[[nodiscard]] inline bool texture_view_valid(const TextureViewDesc& view) noexcept {
    if (!view.texture) {
        return false;
    }

    const auto& desc = view.texture->desc();
    if (view.format != desc.format || view.dimension != desc.dimension ||
        !texture_supports_usage(desc, view.requiredUsage)) {
        return false;
    }

    return view.range.mipLevelCount != 0 &&
           view.range.arrayLayerCount != 0 &&
           view.range.baseMipLevel < desc.mipLevels &&
           view.range.mipLevelCount <= desc.mipLevels - view.range.baseMipLevel &&
           view.range.baseArrayLayer < desc.arrayLayers &&
            view.range.arrayLayerCount <=
                desc.arrayLayers - view.range.baseArrayLayer;
}

[[nodiscard]] inline bool buffer_barrier_valid(
    const BufferBarrierDesc& barrier) noexcept {
    if (!barrier.buffer) {
        return false;
    }
    const auto& desc = barrier.buffer->desc();
    return buffer_supports_state(desc, barrier.before) &&
           buffer_supports_state(desc, barrier.after);
}

[[nodiscard]] inline bool texture_barrier_valid(
    const TextureBarrierDesc& barrier) noexcept {
    if (!barrier.texture) {
        return false;
    }

    const auto& desc = barrier.texture->desc();
    return texture_supports_state(desc, barrier.before) &&
           texture_supports_state(desc, barrier.after) &&
           barrier.range.mipLevelCount != 0 &&
           barrier.range.arrayLayerCount != 0 &&
           barrier.range.baseMipLevel < desc.mipLevels &&
           barrier.range.mipLevelCount <=
               desc.mipLevels - barrier.range.baseMipLevel &&
           barrier.range.baseArrayLayer < desc.arrayLayers &&
           barrier.range.arrayLayerCount <=
               desc.arrayLayers - barrier.range.baseArrayLayer;
}

} // namespace truffle::rhi::validation

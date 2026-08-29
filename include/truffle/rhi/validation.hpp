#pragma once

#include "truffle/rhi/types.hpp"

#include <algorithm>

namespace truffle::rhi::validation {

[[nodiscard]] constexpr bool is_non_zero(Extent2D extent) noexcept {
    return extent.width != 0 && extent.height != 0;
}

[[nodiscard]] constexpr bool is_non_zero(Extent3D extent) noexcept {
    return extent.width != 0 && extent.height != 0 && extent.depth != 0;
}

[[nodiscard]] constexpr bool native_surface_handles_valid(
    const NativeSurface& native) noexcept {
    switch (native.kind) {
    case NativeSurfaceKind::headless:
        return native.handle == nullptr && native.display == nullptr;
    case NativeSurfaceKind::cocoa_layer:
    case NativeSurfaceKind::android_window:
    case NativeSurfaceKind::web_canvas:
    case NativeSurfaceKind::external:
        return native.handle != nullptr;
    case NativeSurfaceKind::win32:
        return native.handle != nullptr;
    case NativeSurfaceKind::wayland:
    case NativeSurfaceKind::xcb:
        return native.handle != nullptr && native.display != nullptr;
    }
    return false;
}

[[nodiscard]] constexpr bool buffer_desc_valid(const BufferDesc& desc) noexcept {
    return desc.size != 0 && desc.usage != BufferUsage::none &&
           !(desc.mappedAtCreation &&
             desc.memory == MemoryDomain::device_local);
}

[[nodiscard]] constexpr TextureAspect format_aspects(
    TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::depth16_unorm:
    case TextureFormat::depth32_float:
        return TextureAspect::depth;
    case TextureFormat::depth24_unorm_stencil8:
    case TextureFormat::depth32_float_stencil8:
        return TextureAspect::depth | TextureAspect::stencil;
    case TextureFormat::unknown:
        return TextureAspect::none;
    default:
        return TextureAspect::color;
    }
}

[[nodiscard]] constexpr bool format_is_compressed(
    TextureFormat format) noexcept {
    return format == TextureFormat::bc1_rgba_unorm ||
           format == TextureFormat::bc1_rgba_srgb ||
           format == TextureFormat::bc3_rgba_unorm ||
           format == TextureFormat::bc3_rgba_srgb;
}

[[nodiscard]] constexpr bool formats_view_compatible(
    TextureFormat resource, TextureFormat view) noexcept {
    if (resource == view) {
        return true;
    }
    return (resource == TextureFormat::rgba8_unorm &&
            view == TextureFormat::rgba8_srgb) ||
           (resource == TextureFormat::rgba8_srgb &&
            view == TextureFormat::rgba8_unorm) ||
           (resource == TextureFormat::bgra8_unorm &&
            view == TextureFormat::bgra8_srgb) ||
           (resource == TextureFormat::bgra8_srgb &&
            view == TextureFormat::bgra8_unorm) ||
           (resource == TextureFormat::bc1_rgba_unorm &&
            view == TextureFormat::bc1_rgba_srgb) ||
           (resource == TextureFormat::bc1_rgba_srgb &&
            view == TextureFormat::bc1_rgba_unorm) ||
           (resource == TextureFormat::bc3_rgba_unorm &&
            view == TextureFormat::bc3_rgba_srgb) ||
           (resource == TextureFormat::bc3_rgba_srgb &&
            view == TextureFormat::bc3_rgba_unorm);
}

[[nodiscard]] constexpr std::uint32_t maximum_mip_levels(
    Extent3D extent) noexcept {
    auto largest = std::max(extent.width, std::max(extent.height, extent.depth));
    std::uint32_t levels = 0;
    while (largest != 0) {
        ++levels;
        largest >>= 1u;
    }
    return levels;
}

[[nodiscard]] constexpr bool texture_desc_valid(
    const TextureDesc& desc) noexcept {
    if (!is_non_zero(desc.extent) || desc.format == TextureFormat::unknown ||
        desc.usage == TextureUsage::none || desc.mipLevels == 0 ||
        desc.arrayLayers == 0 || desc.sampleCount == 0 ||
        desc.mipLevels > maximum_mip_levels(desc.extent) ||
        (desc.sampleCount != 1 && desc.sampleCount != 2 &&
         desc.sampleCount != 4 && desc.sampleCount != 8)) {
        return false;
    }
    switch (desc.dimension) {
    case TextureDimension::d1:
        return desc.extent.height == 1 && desc.extent.depth == 1 &&
               desc.sampleCount == 1;
    case TextureDimension::d2:
        return desc.extent.depth == 1 &&
               (desc.sampleCount == 1 || desc.mipLevels == 1);
    case TextureDimension::d3:
        return desc.arrayLayers == 1 && desc.sampleCount == 1;
    case TextureDimension::cube:
        return desc.extent.width == desc.extent.height &&
               desc.extent.depth == 1 && desc.arrayLayers % 6 == 0 &&
               desc.sampleCount == 1;
    }
    return false;
}

[[nodiscard]] constexpr bool texture_view_desc_valid(
    const TextureDesc& texture, const TextureViewDesc& view) noexcept {
    const auto aspects = format_aspects(texture.format);
    return view.dimension == texture.dimension &&
           formats_view_compatible(texture.format, view.format) &&
           view.range.aspects != TextureAspect::none &&
           has_aspect(aspects, view.range.aspects) &&
           view.range.mipLevelCount != 0 && view.range.arrayLayerCount != 0 &&
           view.range.baseMipLevel < texture.mipLevels &&
           view.range.mipLevelCount <=
               texture.mipLevels - view.range.baseMipLevel &&
           view.range.baseArrayLayer < texture.arrayLayers &&
           view.range.arrayLayerCount <=
               texture.arrayLayers - view.range.baseArrayLayer;
}

[[nodiscard]] inline bool supports_feature(const AdapterInfo& adapter,
                                           Feature feature) {
    return std::find(adapter.supportedFeatures.begin(),
                     adapter.supportedFeatures.end(),
                     feature) != adapter.supportedFeatures.end();
}

[[nodiscard]] inline bool supports_queue(const AdapterInfo& adapter,
                                         QueueKind queue) {
    return std::find(adapter.queueKinds.begin(), adapter.queueKinds.end(), queue) !=
           adapter.queueKinds.end();
}

} // namespace truffle::rhi::validation

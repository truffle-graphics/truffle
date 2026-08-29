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
    return desc.size != 0 && desc.usage != BufferUsage::none;
}

[[nodiscard]] constexpr bool texture_desc_valid(
    const TextureDesc& desc) noexcept {
    return is_non_zero(desc.extent) && desc.format != TextureFormat::unknown &&
           desc.usage != TextureUsage::none && desc.mipLevels != 0 &&
           desc.arrayLayers != 0 && desc.sampleCount != 0;
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

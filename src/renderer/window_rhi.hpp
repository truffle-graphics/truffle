#pragma once

#include "truffle/renderer/window.hpp"
#include "truffle/base/status.hpp"
#include "truffle/rhi/types.hpp"

namespace truffle::renderer {

inline base::Result<rhi::NativeSurface> native_surface(WindowHandle window) {
    rhi::NativeSurfaceKind kind = rhi::NativeSurfaceKind::external;
    switch (window.kind) {
    case WindowKind::headless: kind = rhi::NativeSurfaceKind::headless; break;
    case WindowKind::cocoa_layer: kind = rhi::NativeSurfaceKind::cocoa_layer; break;
    case WindowKind::win32: kind = rhi::NativeSurfaceKind::win32; break;
    case WindowKind::wayland: kind = rhi::NativeSurfaceKind::wayland; break;
    case WindowKind::xcb: kind = rhi::NativeSurfaceKind::xcb; break;
    case WindowKind::android_window: kind = rhi::NativeSurfaceKind::android_window; break;
    case WindowKind::web_canvas: kind = rhi::NativeSurfaceKind::web_canvas; break;
    case WindowKind::external: kind = rhi::NativeSurfaceKind::external; break;
    case WindowKind::cocoa_view: kind = rhi::NativeSurfaceKind::cocoa_view; break;
    default:
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Unknown native window kind");
    }
    return rhi::NativeSurface{kind, window.handle, window.display};
}

} // namespace truffle::renderer

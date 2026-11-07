#pragma once

#include <cstdint>

namespace truffle::base {

enum class WindowKind {
    headless, cocoa_layer, win32, wayland, xcb, android_window,
    web_canvas, external, cocoa_view,
};

// Borrowed platform handle, never a window owner. Metal uses CAMetalLayer;
// Win32 uses HWND with optional HINSTANCE in display; XCB uses its existing
// window/connection convention. Unsupported backend/kind pairs fail explicitly.
struct WindowHandle {
    WindowKind kind = WindowKind::headless;
    void* handle = nullptr;
    void* display = nullptr;
};

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool operator==(const PixelSize&) const = default;
};

struct PixelRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool operator==(const PixelRect&) const = default;
};

} // namespace truffle::base

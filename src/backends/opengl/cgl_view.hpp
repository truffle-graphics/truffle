#pragma once

// C++ boundary for the AppKit objects behind macOS GL presentation. The host
// owns the NSView and its window; Truffle borrows the view, attaches one
// double-buffered NSOpenGLContext that shares objects with the offscreen CGL
// context, and never creates, sizes or destroys windows. AppKit requires
// every call here to run on the main thread.

#include "foundation_backend.hpp"

#include <OpenGL/OpenGL.h>

#include <memory>

namespace truffle::rhi::detail::gl_probe::cgl_view {

struct View;
struct Presenter;

[[nodiscard]] bool on_main_thread() noexcept;

// Borrows and retains a host NSView*; fails unless the handle is an NSView.
[[nodiscard]] Result<std::shared_ptr<View>> borrow_view(void* nsView);

// Backing-pixel size of the view's GL drawable, or 0x0 when the view is not
// in a window. The host's wantsBestResolutionOpenGLSurface choice decides
// whether this is in backing pixels or points.
[[nodiscard]] Extent2D drawable_extent(const View& view);

// Creates a double-buffered GL 4.1 core context that shares objects with
// `share` and attaches it to the view.
[[nodiscard]] Result<std::shared_ptr<Presenter>> create_presenter(
    const std::shared_ptr<View>& view, CGLContextObj share,
    int swapInterval);

[[nodiscard]] CGLContextObj presenter_context(const Presenter& presenter);

// Reattaches the view when needed and refreshes the drawable after the view
// moved or changed size.
void update_drawable(Presenter& presenter);

// Swaps the presentation context's back buffer to the view.
void flush(Presenter& presenter);

} // namespace truffle::rhi::detail::gl_probe::cgl_view

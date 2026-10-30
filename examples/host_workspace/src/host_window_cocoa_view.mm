// macOS-only: hand the GLFW window's content NSView to Truffle as a
// cocoa_view surface for the OpenGL backend, as a void* so the rest of the
// code stays C++.

#include "host_window.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>

namespace truffle::examples::host_workspace {

void* HostWindow::native_view_handle() const noexcept {
    if (window_ == nullptr) {
        return nullptr;
    }
    NSWindow* nsWindow = glfwGetCocoaWindow(window_);
    if (nsWindow == nil) {
        return nullptr;
    }
    NSView* view = nsWindow.contentView;
    // The host chooses a Retina-sized drawable so it matches the GLFW
    // framebuffer extent; AppKit deprecates this with the rest of NSOpenGL.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    view.wantsBestResolutionOpenGLSurface = YES;
#pragma clang diagnostic pop
    return (__bridge void*)view;
}

} // namespace truffle::examples::host_workspace

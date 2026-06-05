// macOS-only: attach a CAMetalLayer to the GLFW window's content view and
// return it as a void* so the rest of the code stays C++.

#include "host_window.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace truffle::examples::host_workspace {

void* HostWindow::native_layer_handle() const noexcept {
    if (window_ == nullptr) {
        return nullptr;
    }
    NSWindow* nsWindow = glfwGetCocoaWindow(window_);
    if (nsWindow == nil) {
        return nullptr;
    }
    NSView* view = nsWindow.contentView;
    if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
        view.wantsLayer = YES;
        view.layer = [CAMetalLayer layer];
    }
    return (__bridge void*)view.layer;
}

} // namespace truffle::examples::host_workspace

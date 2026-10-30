#include "cgl_view.hpp"

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION 1
#endif

#import <AppKit/AppKit.h>

#include <array>
#include <cmath>

// NSOpenGLContext view attachment and wantsBestResolutionOpenGLSurface are
// AppKit-deprecated with the rest of macOS OpenGL; GL_SILENCE_DEPRECATION
// does not cover them, so silence only this translation unit.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace truffle::rhi::detail::gl_probe::cgl_view {

struct View {
    explicit View(NSView* borrowed) : view{[borrowed retain]} {}
    ~View() { [view release]; }

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    NSView* view = nil;
};

struct Presenter {
    Presenter(std::shared_ptr<View> owner, NSOpenGLContext* created)
        : view{std::move(owner)}, context{created} {}
    ~Presenter() {
        if (context == nil) {
            return;
        }
        if ([NSOpenGLContext currentContext] == context) {
            [NSOpenGLContext clearCurrentContext];
        }
        [context clearDrawable];
        [context release];
    }

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    std::shared_ptr<View> view;
    NSOpenGLContext* context = nil;
};

bool on_main_thread() noexcept {
    return [NSThread isMainThread] == YES;
}

Result<std::shared_ptr<View>> borrow_view(void* nsView) {
    @autoreleasepool {
        id object = (__bridge id)nsView;
        if (object == nil || ![object isKindOfClass:[NSView class]]) {
            return Status::failure(StatusCode::invalid_argument,
                                   "GL cocoa_view surface handle is not an NSView");
        }
        return std::make_shared<View>(static_cast<NSView*>(object));
    }
}

Extent2D drawable_extent(const View& view) {
    @autoreleasepool {
        if (view.view == nil || view.view.window == nil) {
            return {0, 0};
        }
        NSRect bounds = view.view.bounds;
        if (view.view.wantsBestResolutionOpenGLSurface) {
            bounds = [view.view convertRectToBacking:bounds];
        }
        const auto width = std::lround(NSWidth(bounds));
        const auto height = std::lround(NSHeight(bounds));
        if (width <= 0 || height <= 0) {
            return {0, 0};
        }
        return {static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height)};
    }
}

Result<std::shared_ptr<Presenter>> create_presenter(
    const std::shared_ptr<View>& view, CGLContextObj share, int swapInterval) {
    @autoreleasepool {
        if (!view || view->view == nil) {
            return Status::failure(StatusCode::surface_lost,
                                   "GL presentation view is no longer available");
        }
        // Matches the offscreen context's renderer set so objects can be
        // shared, adding the double-buffered window drawable.
        const std::array<CGLPixelFormatAttribute, 10> attributes{
            kCGLPFAOpenGLProfile,
            static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core),
            kCGLPFAAccelerated,
            kCGLPFADoubleBuffer,
            kCGLPFAColorSize,
            static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAAlphaSize,
            static_cast<CGLPixelFormatAttribute>(8),
            kCGLPFAAllowOfflineRenderers,
            static_cast<CGLPixelFormatAttribute>(0),
        };
        CGLPixelFormatObj pixelFormat = nullptr;
        GLint formatCount = 0;
        auto error = CGLChoosePixelFormat(attributes.data(), &pixelFormat,
                                          &formatCount);
        if (error != kCGLNoError || pixelFormat == nullptr || formatCount == 0) {
            if (pixelFormat != nullptr) {
                CGLReleasePixelFormat(pixelFormat);
            }
            return Status::failure(
                StatusCode::unavailable,
                "CGL found no double-buffered GL 4.1 core pixel format");
        }
        CGLContextObj created = nullptr;
        error = CGLCreateContext(pixelFormat, share, &created);
        CGLReleasePixelFormat(pixelFormat);
        if (error != kCGLNoError || created == nullptr) {
            return Status::failure(
                StatusCode::unavailable,
                "CGL could not create a presentation context sharing the device");
        }
        NSOpenGLContext* context =
            [[NSOpenGLContext alloc] initWithCGLContextObj:created];
        CGLReleaseContext(created);
        if (context == nil) {
            return Status::failure(StatusCode::unavailable,
                                   "NSOpenGLContext could not wrap the CGL context");
        }
        const GLint interval = swapInterval;
        [context setValues:&interval
              forParameter:NSOpenGLContextParameterSwapInterval];
        auto presenter = std::make_shared<Presenter>(view, context);
        update_drawable(*presenter);
        return presenter;
    }
}

CGLContextObj presenter_context(const Presenter& presenter) {
    return presenter.context != nil ? presenter.context.CGLContextObj : nullptr;
}

void update_drawable(Presenter& presenter) {
    @autoreleasepool {
        if (presenter.context == nil || presenter.view->view.window == nil) {
            return;
        }
        if (presenter.context.view != presenter.view->view) {
            presenter.context.view = presenter.view->view;
        }
        [presenter.context update];
    }
}

void flush(Presenter& presenter) {
    @autoreleasepool {
        [presenter.context flushBuffer];
    }
}

} // namespace truffle::rhi::detail::gl_probe::cgl_view

#pragma clang diagnostic pop

#pragma once

// Desktop GL presentation into a host-owned NSView or Win32 HWND.
//
// The device keeps rendering into an offscreen CGL context. Each swapchain
// owns one back-buffer texture (image index 0) in that context and a second,
// double-buffered presentation context attached to the host view that shares
// its objects. Present blits the back buffer into the view's default
// framebuffer with a vertical flip (texture row 0 is the top of the window)
// and swaps. AppKit requires the main thread; WGL requires the window's
// owning thread. Host windows and event loops are never owned here.

#include "gl_probe.hpp"

#if defined(TFL_GL_CONTEXT_WGL)
#include "wgl_view.hpp"
#else
#include "cgl_view.hpp"
#endif
#include "opengl_backend_test.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace truffle::rhi::detail::gl_probe {

#if defined(TFL_GL_CONTEXT_WGL)
namespace native_view = wgl_view;
#else
namespace native_view = cgl_view;
#endif

struct PresentCaptureState {
    std::atomic_bool enabled{false};
    std::mutex mutex;
    OpenGLPresentCapture last;
};

[[nodiscard]] inline PresentCaptureState& present_capture_state() {
    static PresentCaptureState state;
    return state;
}

[[nodiscard]] inline Status require_main_thread() {
#if defined(TFL_GL_CONTEXT_WGL)
    return Status::success();
#else
    return cgl_view::on_main_thread()
               ? Status::success()
               : Status::failure(
                     StatusCode::invalid_state,
                     "macOS GL presentation must run on the AppKit main thread");
#endif
}

struct SurfaceResource {
    std::shared_ptr<Context> context;
    std::shared_ptr<native_view::View> view;
};

[[nodiscard]] inline Status presentation_view_status(const SurfaceResource& surface) {
#if defined(TFL_GL_CONTEXT_WGL)
    return wgl_view::view_status(*surface.view);
#else
    (void)surface;
    return require_main_thread();
#endif
}

[[nodiscard]] inline Status make_presenter_current(
    native_view::Presenter& presenter,
    StatusCode failureCode = StatusCode::surface_lost) {
#if defined(TFL_GL_CONTEXT_WGL)
    (void)failureCode;
    return wgl_view::make_current(presenter);
#else
    const auto error = CGLSetCurrentContext(cgl_view::presenter_context(presenter));
    return error == kCGLNoError ? Status::success()
        : cgl_failure(failureCode,
                      "CGL could not bind the presentation context", error);
#endif
}

struct SwapchainResource {
    ~SwapchainResource() {
        if (!context || !presenter) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (readFramebuffer != 0 && make_presenter_current(*presenter).ok()) {
            glDeleteFramebuffers(1, &readFramebuffer);
        }
        presenter.reset();
        (void)make_current(*context);
    }

    std::shared_ptr<Context> context;
    std::shared_ptr<SurfaceResource> surface;
    std::shared_ptr<native_view::Presenter> presenter;
    std::shared_ptr<TextureResource> image;
    SwapchainDesc desc;
    Extent2D drawable;
    GLuint readFramebuffer = 0;
    bool acquired = false;
    std::mutex mutex;
};

[[nodiscard]] inline bool same_extent(Extent2D lhs, Extent2D rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

[[nodiscard]] inline Result<std::shared_ptr<TextureResource>>
create_back_buffer(const std::shared_ptr<Context>& context,
                   const SwapchainDesc& desc) {
    auto created = create_texture(
        context, TextureDesc{
                     .extent = {desc.extent.width, desc.extent.height, 1},
                     .format = desc.format,
                     .usage = TextureUsage::color_attachment |
                              TextureUsage::copy_source | TextureUsage::present,
                     .debugName = desc.debugName + " back buffer",
                 });
    if (!created.ok()) {
        return created.status();
    }
    return std::static_pointer_cast<TextureResource>(std::move(created).value());
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_surface(
    const std::shared_ptr<void>& nativeContext, const SurfaceDesc& desc) {
    auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
#if defined(TFL_GL_CONTEXT_WGL)
    constexpr auto surfaceKind = NativeSurfaceKind::win32;
    constexpr auto surfaceMessage = "Windows GL surfaces require a host-owned win32 HWND";
#else
    constexpr auto surfaceKind = NativeSurfaceKind::cocoa_view;
    constexpr auto surfaceMessage = "macOS GL surfaces require a host-owned cocoa_view NSView";
#endif
    if (desc.native.kind != surfaceKind) {
        return Status::failure(
            StatusCode::unsupported, surfaceMessage);
    }
    if (auto status = require_main_thread(); !status.ok()) {
        return status;
    }
    auto view = native_view::borrow_view(desc.native.handle);
    if (!view.ok()) {
        return view.status();
    }
    auto surface = std::make_shared<SurfaceResource>();
    surface->context = std::move(context);
    surface->view = std::move(view).value();
    return std::static_pointer_cast<void>(std::move(surface));
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_swapchain(
    const std::shared_ptr<void>& nativeContext,
    const std::shared_ptr<void>& nativeSurface, const SwapchainDesc& desc) {
    auto context = std::static_pointer_cast<Context>(nativeContext);
    auto surface = std::static_pointer_cast<SurfaceResource>(nativeSurface);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (!surface || !surface->view) {
        return Status::failure(StatusCode::surface_lost,
                               "GL surface is no longer available");
    }
    if (auto status = presentation_view_status(*surface); !status.ok()) {
        return status;
    }
    if (auto status = require_main_thread(); !status.ok()) {
        return status;
    }
    if (desc.format != TextureFormat::rgba8_unorm &&
        desc.format != TextureFormat::bgra8_unorm) {
        return Status::failure(
            StatusCode::unsupported,
            "GL swapchains support rgba8_unorm and bgra8_unorm only");
    }
    if (desc.presentMode == PresentMode::mailbox) {
        return Status::failure(
            StatusCode::unsupported,
            "GL has no mailbox presentation; use fifo or immediate");
    }
    auto image = create_back_buffer(context, desc);
    if (!image.ok()) {
        return image.status();
    }
    auto swapchain = std::make_shared<SwapchainResource>();
    swapchain->context = context;
    swapchain->surface = std::move(surface);
    swapchain->image = std::move(image).value();
    swapchain->desc = desc;
    std::lock_guard lock{context->mutex};
#if defined(TFL_GL_CONTEXT_WGL)
    auto presenter = wgl_view::create_presenter(
        swapchain->surface->view, *context,
        desc.presentMode == PresentMode::fifo ? 1 : 0);
#else
    auto presenter = cgl_view::create_presenter(
        swapchain->surface->view, context->context,
        desc.presentMode == PresentMode::fifo ? 1 : 0);
#endif
    if (!presenter.ok()) {
        (void)make_current(*context);
        return presenter.status();
    }
    swapchain->presenter = std::move(presenter).value();
    swapchain->drawable = native_view::drawable_extent(*swapchain->surface->view);
    if (auto status = make_presenter_current(*swapchain->presenter,
                                              StatusCode::unavailable);
        !status.ok()) {
        (void)make_current(*context);
        return status;
    }
    glGenFramebuffers(1, &swapchain->readFramebuffer);
    const bool failed =
        glGetError() != GL_NO_ERROR || swapchain->readFramebuffer == 0;
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    if (failed) {
        return Status::failure(StatusCode::backend_error,
                               "GL presentation framebuffer creation failed");
    }
    return std::static_pointer_cast<void>(std::move(swapchain));
}

[[nodiscard]] inline Result<NativeSwapchainImage> acquire_swapchain(
    const std::shared_ptr<void>& nativeSwapchain) {
    const auto swapchain =
        std::static_pointer_cast<SwapchainResource>(nativeSwapchain);
    if (!swapchain || !swapchain->surface || !swapchain->presenter) {
        return Status::failure(StatusCode::surface_lost,
                               "GL surface is no longer available");
    }
    if (auto status = require_main_thread(); !status.ok()) {
        return status;
    }
    std::lock_guard lock{swapchain->mutex};
    if (swapchain->acquired) {
        return Status::failure(StatusCode::invalid_state,
                               "GL swapchain image is already acquired");
    }
    if (auto status = presentation_view_status(*swapchain->surface); !status.ok()) {
        return status;
    }
    const auto drawable = native_view::drawable_extent(*swapchain->surface->view);
    if (drawable.width == 0 || drawable.height == 0) {
        return Status::failure(
            StatusCode::out_of_date,
            "GL view has no drawable: it is zero-sized or not in a window");
    }
    swapchain->acquired = true;
    NativeSwapchainImage result;
    result.texture = swapchain->image;
    result.imageIndex = 0;
    result.extent = swapchain->desc.extent;
    if (!same_extent(drawable, swapchain->desc.extent)) {
        result.status = Status::failure(
            StatusCode::suboptimal,
            "GL view drawable extent differs from the swapchain extent");
    }
    return result;
}

[[nodiscard]] inline Status resize_swapchain(
    const std::shared_ptr<void>& nativeSwapchain, Extent2D extent) {
    const auto swapchain =
        std::static_pointer_cast<SwapchainResource>(nativeSwapchain);
    if (!swapchain || !swapchain->surface || !swapchain->presenter) {
        return Status::failure(StatusCode::surface_lost,
                               "GL surface is no longer available");
    }
    if (auto status = require_main_thread(); !status.ok()) {
        return status;
    }
    std::lock_guard lock{swapchain->mutex};
    if (swapchain->acquired) {
        return Status::failure(StatusCode::invalid_state,
                               "GL swapchain cannot resize an acquired image");
    }
    if (auto status = presentation_view_status(*swapchain->surface); !status.ok()) {
        return status;
    }
    auto desc = swapchain->desc;
    desc.extent = extent;
    auto image = create_back_buffer(swapchain->context, desc);
    if (!image.ok()) {
        return image.status();
    }
    swapchain->image = std::move(image).value();
    swapchain->desc = desc;
    std::lock_guard contextLock{swapchain->context->mutex};
#if defined(TFL_GL_CONTEXT_CGL)
    cgl_view::update_drawable(*swapchain->presenter);
#endif
    swapchain->drawable = native_view::drawable_extent(*swapchain->surface->view);
    return make_current(*swapchain->context);
}

inline void capture_presented_pixels(Extent2D drawable) {
    auto& capture = present_capture_state();
    if (!capture.enabled.load()) {
        return;
    }
    const auto rowBytes = static_cast<std::size_t>(drawable.width) * 4u;
    std::vector<std::uint8_t> bottomUp(rowBytes * drawable.height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(drawable.width),
                 static_cast<GLsizei>(drawable.height), GL_RGBA,
                 GL_UNSIGNED_BYTE, bottomUp.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    OpenGLPresentCapture result{.extent = drawable, .rgba = {}};
    result.rgba.resize(bottomUp.size());
    for (std::uint32_t row = 0; row < drawable.height; ++row) {
        std::memcpy(result.rgba.data() + row * rowBytes,
                    bottomUp.data() + (drawable.height - 1 - row) * rowBytes,
                    rowBytes);
    }
    std::lock_guard lock{capture.mutex};
    capture.last = std::move(result);
}

[[nodiscard]] inline Status present_swapchain(
    const std::shared_ptr<void>& nativeSwapchain, std::uint32_t imageIndex,
    std::span<const NativeSemaphorePoint>) {
    const auto swapchain =
        std::static_pointer_cast<SwapchainResource>(nativeSwapchain);
    if (!swapchain || !swapchain->surface || !swapchain->presenter) {
        return Status::failure(StatusCode::surface_lost,
                               "GL surface is no longer available");
    }
    if (auto status = require_main_thread(); !status.ok()) {
        return status;
    }
    std::lock_guard lock{swapchain->mutex};
    if (!swapchain->acquired || imageIndex != 0) {
        return Status::failure(StatusCode::invalid_state,
                               "GL presentation image is not acquired");
    }
    // Present waits were reached on the host before this call and every GL
    // submission completes before it returns, so the image is final here.
    auto& context = *swapchain->context;
    std::lock_guard contextLock{context.mutex};
    if (auto status = presentation_view_status(*swapchain->surface); !status.ok()) {
        return status;
    }
    const auto drawable = native_view::drawable_extent(*swapchain->surface->view);
    if (drawable.width == 0 || drawable.height == 0) {
        swapchain->acquired = false;
        return Status::failure(
            StatusCode::out_of_date,
            "GL view has no drawable: it is zero-sized or not in a window");
    }
    // Reattaches a view the host moved between windows and tracks live
    // resizes before drawing.
#if defined(TFL_GL_CONTEXT_CGL)
    cgl_view::update_drawable(*swapchain->presenter);
#endif
    swapchain->drawable = drawable;
#if defined(TFL_GL_CONTEXT_WGL)
    wgl_view::DrawableBinding binding{*swapchain->presenter};
    if (auto status = binding.bind(); !status.ok()) {
        return status;
    }
#else
    if (auto status = make_presenter_current(*swapchain->presenter); !status.ok()) {
        return status;
    }
#endif
    const auto restore = [&](Status status) {
        if (status.ok() || status.code == StatusCode::suboptimal ||
            status.code == StatusCode::out_of_date) {
            swapchain->acquired = false;
        }
        if (auto restored = make_current(context); !restored.ok()) {
            return restored;
        }
        return status;
    };
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        return restore(Status::failure(
            StatusCode::out_of_date,
            "GL view drawable is not ready for presentation"));
    }
    const auto& image = *swapchain->image;
    const auto width = static_cast<GLint>(image.desc.extent.width);
    const auto height = static_cast<GLint>(image.desc.extent.height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, swapchain->readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, image.name, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    const bool exact = same_extent(drawable, swapchain->desc.extent);
    glBlitFramebuffer(0, 0, width, height, 0,
                      static_cast<GLint>(drawable.height),
                      static_cast<GLint>(drawable.width), 0,
                      GL_COLOR_BUFFER_BIT, exact ? GL_NEAREST : GL_LINEAR);
    capture_presented_pixels(drawable);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, swapchain->readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    auto error = glGetError();
#if defined(TFL_GL_CONTEXT_WGL)
    if (auto status = wgl_view::flush(binding.dc); !status.ok()) {
        return restore(status);
    }
#else
    cgl_view::flush(*swapchain->presenter);
#endif
    // The device context reuses the back buffer next frame; the blit read
    // in this context must complete first.
    if (auto status = wait_for_completion("GL presentation"); !status.ok()) {
        return restore(status);
    }
    if (error == GL_NO_ERROR) {
        error = glGetError();
    }
    if (error != GL_NO_ERROR) {
        return restore(Status::failure(StatusCode::backend_validation_failed,
                                       "GL presentation reported an error"));
    }
    return restore(exact ? Status::success()
                         : Status::failure(
                               StatusCode::suboptimal,
                               "GL view drawable extent differs from the "
                               "swapchain extent; the frame was scaled"));
}

} // namespace truffle::rhi::detail::gl_probe

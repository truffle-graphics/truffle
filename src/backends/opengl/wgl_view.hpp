#pragma once

#include "wgl_context.hpp"

#include <memory>

namespace truffle::rhi::detail::gl_probe::wgl_view {

struct View {
    HWND window = nullptr;
    DWORD thread = 0;
};

[[nodiscard]] inline Status view_status(const View& view) {
    if (!IsWindow(view.window)) {
        return Status::failure(StatusCode::surface_lost,
                               "the host-owned WGL window was destroyed");
    }
    if (GetCurrentThreadId() != view.thread) {
        return Status::failure(StatusCode::invalid_state,
                               "WGL presentation must run on the host window thread");
    }
    RECT rectangle{};
    if (!GetClientRect(view.window, &rectangle)) {
        return wgl_failure(StatusCode::surface_lost,
                           "WGL could not query the host window drawable");
    }
    return Status::success();
}

[[nodiscard]] inline Result<std::shared_ptr<View>> borrow_view(void* handle) {
    const auto window = static_cast<HWND>(handle);
    if (window == nullptr || !IsWindow(window)) {
        return Status::failure(StatusCode::invalid_argument,
                               "a valid host-owned WGL HWND is required");
    }
    DWORD process = 0;
    const auto thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) {
        return Status::failure(StatusCode::invalid_argument,
                               "WGL requires a window owned by the host process");
    }
    auto view = std::make_shared<View>(View{window, thread});
    if (auto status = view_status(*view); !status.ok()) {
        return status;
    }
    return view;
}

[[nodiscard]] inline Extent2D drawable_extent(const View& view) {
    RECT rectangle{};
    if (IsIconic(view.window) || !GetClientRect(view.window, &rectangle)) {
        return {0, 0};
    }
    return {static_cast<std::uint32_t>(rectangle.right - rectangle.left),
            static_cast<std::uint32_t>(rectangle.bottom - rectangle.top)};
}

struct Presenter {
    ~Presenter() {
        if (context != nullptr) {
            if (wglGetCurrentContext() == context) {
                if (!wglMakeCurrent(nullptr, nullptr)) {
                    report_wgl_cleanup_failure("unbind presentation context");
                }
            }
            if (!wglDeleteContext(context)) {
                report_wgl_cleanup_failure("delete presentation context");
            }
        }
    }
    std::shared_ptr<View> view;
    Context* owner = nullptr;
    HGLRC context = nullptr;
};

[[nodiscard]] inline Result<std::shared_ptr<Presenter>> create_presenter(
    const std::shared_ptr<View>& view, Context& owner, int interval) {
    if (auto status = view_status(*view); !status.ok()) {
        return status;
    }
    const auto dc = GetDC(view->window);
    if (dc == nullptr) {
        return wgl_failure(StatusCode::surface_lost,
                           "WGL could not acquire the host window DC");
    }
    struct Release {
        HWND window;
        HDC dc;
        ~Release() {
            release_wgl_window_dc(window, dc);
        }
    } release{view->window, dc};
    auto format = GetPixelFormat(dc);
    if (format == 0) {
        PIXELFORMATDESCRIPTOR descriptor{};
        if (DescribePixelFormat(dc, owner.pixelFormat, sizeof(descriptor),
                                &descriptor) == 0 ||
            !SetPixelFormat(dc, owner.pixelFormat, &descriptor)) {
            return wgl_failure(StatusCode::unsupported,
                               "WGL could not set a compatible host pixel format");
        }
        format = owner.pixelFormat;
    }
    if (format != owner.pixelFormat) {
        return Status::failure(
            StatusCode::unsupported,
            "the host window pixel format cannot share the WGL device context");
    }
    auto presenter = std::make_shared<Presenter>();
    presenter->view = view;
    presenter->owner = &owner;
    const auto attributes = context_attributes(owner);
    presenter->context = owner.createContext(dc, owner.context, attributes.data());
    if (presenter->context == nullptr || !wglMakeCurrent(dc, presenter->context)) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL could not create the shared presentation context");
    }
    wgl_api::current = &owner.api;
    const bool intervalSet = owner.swapInterval(interval) != FALSE;
    if (owner.debugOutput) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&debug_message, &owner);
    }
    const auto restored = make_current(owner);
    if (!restored.ok()) {
        return restored;
    }
    if (!intervalSet) {
        return wgl_failure(StatusCode::unsupported,
                           "WGL could not set the requested swap interval");
    }
    return presenter;
}

// Use the permanent pbuffer DC for context-local cleanup, even after HWND loss.
[[nodiscard]] inline Status make_current(Presenter& presenter) {
    if (!context_available(*presenter.owner)) {
        return Status::failure(StatusCode::device_lost,
                               "the native WGL context is unavailable");
    }
    if (!wglMakeCurrent(presenter.owner->dc, presenter.context)) {
        return wgl_failure(StatusCode::device_lost,
                           "WGL could not bind the presentation context");
    }
    wgl_api::current = &presenter.owner->api;
    return Status::success();
}

struct DrawableBinding {
    explicit DrawableBinding(Presenter& value) : presenter(value) {}
    ~DrawableBinding() {
        if (dc != nullptr) {
            if (wglGetCurrentDC() == dc) {
                if (auto status = gl_probe::make_current(*presenter.owner);
                    !status.ok()) {
                    presenter.owner->lost.store(true);
                    ++presenter.owner->validationErrors;
                    std::fprintf(stderr, "%s\n", status.message.c_str());
                }
            }
            release_wgl_window_dc(presenter.view->window, dc);
        }
    }

    [[nodiscard]] Status bind() {
        dc = GetDC(presenter.view->window);
        if (dc == nullptr || !wglMakeCurrent(dc, presenter.context)) {
            return wgl_failure(StatusCode::surface_lost,
                               "WGL could not bind the host window drawable");
        }
        wgl_api::current = &presenter.owner->api;
        return Status::success();
    }

    Presenter& presenter;
    HDC dc = nullptr;
};

[[nodiscard]] inline Status flush(HDC dc) {
    return SwapBuffers(dc) ? Status::success()
                           : wgl_failure(StatusCode::surface_lost,
                                         "WGL SwapBuffers failed");
}

} // namespace truffle::rhi::detail::gl_probe::wgl_view

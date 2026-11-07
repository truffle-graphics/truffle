#pragma once

#include "foundation_backend.hpp"
#include "gl_api.hpp"

#include <GL/wglext.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace truffle::rhi::detail::gl_probe {

inline std::atomic_bool contextLossForTesting{false};

inline void report_wgl_cleanup_failure(const char* operation) {
    std::fprintf(stderr, "WGL cleanup failed: %s (%lu)\n",
                 operation, GetLastError());
}

inline void release_wgl_window_dc(HWND window, HDC dc) {
    if (IsWindow(window) &&
        (GetClassLongPtrW(window, GCL_STYLE) & (CS_OWNDC | CS_CLASSDC)) == 0 &&
        ReleaseDC(window, dc) == 0) {
        report_wgl_cleanup_failure("release host window DC");
    }
}

[[nodiscard]] inline Status wgl_failure(StatusCode code, std::string message) {
    BackendDiagnostic diagnostic{
        .domain = "wgl",
        .nativeCode = static_cast<std::int64_t>(GetLastError()),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(diagnostic));
}

struct Context;

struct ContextMutex {
    explicit ContextMutex(Context& ownerValue) : owner(ownerValue) {}
    void lock();
    void unlock();
    Context& owner;
    std::mutex mutex;
    HGLRC previousContext = nullptr;
    HDC previousDC = nullptr;
    wgl_api::Api* previousApi = nullptr;
};

struct Context {
    Context() : mutex(*this) {}
    ~Context() {
        if (context != nullptr && !wglDeleteContext(context)) {
            report_wgl_cleanup_failure("delete context");
        }
        if (pbuffer != nullptr) {
            if (dc != nullptr) {
                if (releasePbufferDC(pbuffer, dc) == 0) {
                    report_wgl_cleanup_failure("release pbuffer DC");
                }
            }
            if (!destroyPbuffer(pbuffer)) {
                report_wgl_cleanup_failure("destroy pbuffer");
            }
        }
    }

    HGLRC context = nullptr;
    HDC dc = nullptr;
    HPBUFFERARB pbuffer = nullptr;
    int pixelFormat = 0;
    PFNWGLCREATECONTEXTATTRIBSARBPROC createContext = nullptr;
    PFNWGLRELEASEPBUFFERDCARBPROC releasePbufferDC = nullptr;
    PFNWGLDESTROYPBUFFERARBPROC destroyPbuffer = nullptr;
    PFNWGLSWAPINTERVALEXTPROC swapInterval = nullptr;
    wgl_api::Api api;
    void (*debugCallback)(const BackendDiagnostic&, void*) = nullptr;
    void* debugUserData = nullptr;
    std::atomic_uint validationErrors{0};
    std::atomic_bool lost{false};
    bool debugOutput = false;
    bool validationRequested = false;
    bool timestampQueries = false;
    bool robust = false;
    ContextMutex mutex;
};

inline void ContextMutex::lock() {
    mutex.lock();
    previousContext = wglGetCurrentContext();
    previousDC = wglGetCurrentDC();
    previousApi = wgl_api::current;
}

inline void ContextMutex::unlock() {
    if ((wglGetCurrentContext() != previousContext ||
         wglGetCurrentDC() != previousDC) &&
        !wglMakeCurrent(previousDC, previousContext)) {
        owner.lost.store(true);
        ++owner.validationErrors;
        const auto failure = wgl_failure(
            StatusCode::device_lost, "WGL could not restore the caller's context");
        std::fprintf(stderr, "%s\n", failure.message.c_str());
        if (owner.debugCallback != nullptr && failure.detail) {
            owner.debugCallback(*failure.detail, owner.debugUserData);
        }
    }
    wgl_api::current = previousApi;
    mutex.unlock();
}

[[nodiscard]] inline bool context_available(const Context& context) noexcept {
    return context.context != nullptr && context.dc != nullptr &&
           !context.lost.load();
}

[[nodiscard]] inline Status make_current(Context& context) {
    if (contextLossForTesting.load()) {
        return Status::failure(StatusCode::device_lost,
                               "injected WGL context loss");
    }
    if (!context_available(context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native WGL context is unavailable");
    }
    if (!wglMakeCurrent(context.dc, context.context)) {
        context.lost.store(true);
        return wgl_failure(StatusCode::device_lost,
                           "WGL could not bind the native offscreen context");
    }
    wgl_api::current = &context.api;
    const auto reset = glGetGraphicsResetStatus();
    if (reset != GL_NO_ERROR) {
        context.lost.store(true);
        return Status::failure(
            StatusCode::device_lost, "the native OpenGL context was reset",
            BackendDiagnostic{.domain = "opengl",
                              .nativeCode = static_cast<std::int64_t>(reset),
                              .objectLabel = {},
                              .message = "the native OpenGL context was reset"});
    }
    return Status::success();
}

[[nodiscard]] inline std::vector<int> context_attributes(const Context& context) {
    std::vector<int> attributes{
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,
        (context.validationRequested ? WGL_CONTEXT_DEBUG_BIT_ARB : 0) |
            (context.robust ? WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB : 0),
    };
    if (context.robust) {
        attributes.push_back(WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB);
        attributes.push_back(WGL_LOSE_CONTEXT_ON_RESET_ARB);
    }
    attributes.push_back(0);
    return attributes;
}

using ContextProc = void (*)();
[[nodiscard]] inline ContextProc context_proc_address(const char* name) {
    return std::bit_cast<ContextProc>(wgl_api::resolve(name));
}

struct WglBootstrap {
    ~WglBootstrap() {
        if (context != nullptr) {
            if (wglGetCurrentContext() == context) {
                if (!wglMakeCurrent(nullptr, nullptr)) {
                    report_wgl_cleanup_failure("unbind bootstrap context");
                }
            }
            if (!wglDeleteContext(context)) {
                report_wgl_cleanup_failure("delete bootstrap context");
            }
        }
        // CS_OWNDC ties the bootstrap DC's lifetime to its window.
        if (window != nullptr) {
            if (!DestroyWindow(window)) {
                report_wgl_cleanup_failure("destroy bootstrap window");
            }
        }
        if (atom != 0) {
            if (!UnregisterClassW(className, GetModuleHandleW(nullptr))) {
                report_wgl_cleanup_failure("unregister bootstrap class");
            }
        }
    }
    static constexpr auto className = L"TruffleWGLBootstrap";
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    ATOM atom = 0;
};

[[nodiscard]] inline Status create_context(Context& native) {
    static std::mutex bootstrapMutex;
    std::lock_guard bootstrapLock{bootstrapMutex};
    WglBootstrap bootstrap;
    const WNDCLASSW windowClass{
        .style = CS_OWNDC,
        .lpfnWndProc = DefWindowProcW,
        .hInstance = GetModuleHandleW(nullptr),
        .lpszClassName = WglBootstrap::className,
    };
    bootstrap.atom = RegisterClassW(&windowClass);
    if (bootstrap.atom == 0) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL bootstrap class registration failed");
    }
    bootstrap.window = CreateWindowExW(
        0, WglBootstrap::className, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
        nullptr, windowClass.hInstance, nullptr);
    if (bootstrap.window == nullptr ||
        (bootstrap.dc = GetDC(bootstrap.window)) == nullptr) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL bootstrap drawable creation failed");
    }
    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 24;
    descriptor.cAlphaBits = 8;
    const auto legacyFormat = ChoosePixelFormat(bootstrap.dc, &descriptor);
    if (legacyFormat == 0 ||
        !SetPixelFormat(bootstrap.dc, legacyFormat, &descriptor) ||
        (bootstrap.context = wglCreateContext(bootstrap.dc)) == nullptr ||
        !wglMakeCurrent(bootstrap.dc, bootstrap.context)) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL bootstrap context creation failed");
    }
    native.createContext = std::bit_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wgl_api::resolve("wglCreateContextAttribsARB"));
    const auto chooseFormat = std::bit_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
        wgl_api::resolve("wglChoosePixelFormatARB"));
    const auto createPbuffer = std::bit_cast<PFNWGLCREATEPBUFFERARBPROC>(
        wgl_api::resolve("wglCreatePbufferARB"));
    const auto getPbufferDC = std::bit_cast<PFNWGLGETPBUFFERDCARBPROC>(
        wgl_api::resolve("wglGetPbufferDCARB"));
    native.releasePbufferDC = std::bit_cast<PFNWGLRELEASEPBUFFERDCARBPROC>(
        wgl_api::resolve("wglReleasePbufferDCARB"));
    native.destroyPbuffer = std::bit_cast<PFNWGLDESTROYPBUFFERARBPROC>(
        wgl_api::resolve("wglDestroyPbufferARB"));
    native.swapInterval = std::bit_cast<PFNWGLSWAPINTERVALEXTPROC>(
        wgl_api::resolve("wglSwapIntervalEXT"));
    const auto getExtensions = std::bit_cast<PFNWGLGETEXTENSIONSSTRINGARBPROC>(
        wgl_api::resolve("wglGetExtensionsStringARB"));
    if (native.createContext == nullptr || chooseFormat == nullptr ||
        createPbuffer == nullptr || getPbufferDC == nullptr ||
        native.releasePbufferDC == nullptr || native.destroyPbuffer == nullptr ||
        native.swapInterval == nullptr) {
        return Status::failure(
            StatusCode::unavailable,
            "WGL requires ARB_create_context, ARB_pixel_format, ARB_pbuffer and EXT_swap_control");
    }
    if (getExtensions != nullptr) {
        if (const auto* extensions = getExtensions(bootstrap.dc);
            extensions != nullptr) {
            native.robust = (" " + std::string{extensions} + " ").find(
                " WGL_ARB_create_context_robustness ") != std::string::npos;
        }
    }
    const std::array attributes{
        WGL_DRAW_TO_PBUFFER_ARB, TRUE, WGL_DRAW_TO_WINDOW_ARB, TRUE,
        WGL_SUPPORT_OPENGL_ARB, TRUE, WGL_DOUBLE_BUFFER_ARB, TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB, WGL_COLOR_BITS_ARB, 24,
        WGL_ALPHA_BITS_ARB, 8, 0,
    };
    UINT count = 0;
    if (!chooseFormat(bootstrap.dc, attributes.data(), nullptr, 1,
                      &native.pixelFormat, &count) || count == 0) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL found no compatible offscreen/window pixel format");
    }
    constexpr std::array pbufferAttributes{0};
    native.pbuffer = createPbuffer(bootstrap.dc, native.pixelFormat, 1, 1,
                                   pbufferAttributes.data());
    if (native.pbuffer == nullptr ||
        (native.dc = getPbufferDC(native.pbuffer)) == nullptr) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL pbuffer creation failed");
    }
    const auto contextAttributes = context_attributes(native);
    native.context = native.createContext(native.dc, nullptr, contextAttributes.data());
    if (native.context == nullptr ||
        !wglMakeCurrent(native.dc, native.context)) {
        return wgl_failure(StatusCode::unavailable,
                           "WGL requires an OpenGL 4.5 core context");
    }
    if (auto status = wgl_api::load(native.api); !status.ok()) {
        return status;
    }
    wgl_api::current = &native.api;
    return Status::success();
}

} // namespace truffle::rhi::detail::gl_probe

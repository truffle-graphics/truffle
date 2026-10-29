#pragma once

// EGL owns the native GL context on Linux. The surfaceless display and the
// 1x1 pbuffer exist only to make a context current; rendering targets FBOs.

#include "foundation_backend.hpp"
#include "gl_api.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <string>

namespace truffle::rhi::detail::gl_probe {

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

[[nodiscard]] inline Status egl_failure(StatusCode code, std::string message,
                                        EGLint error = eglGetError()) {
    BackendDiagnostic detail{
        .domain = "egl",
        .nativeCode = static_cast<std::int64_t>(error),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(detail));
}

struct Context {
    ~Context() {
        if (display == EGL_NO_DISPLAY) {
            return;
        }
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
    }

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    void (*debugCallback)(const BackendDiagnostic&, void*) = nullptr;
    void* debugUserData = nullptr;
    std::atomic_uint validationErrors{0};
    bool debugOutput = false;
    std::mutex mutex;
};

[[nodiscard]] inline bool context_available(const Context& context) noexcept {
    return context.display != EGL_NO_DISPLAY && context.context != EGL_NO_CONTEXT;
}

[[nodiscard]] inline Status make_current(Context& context) {
    return eglMakeCurrent(context.display, context.surface, context.surface,
                          context.context) == EGL_TRUE
               ? Status::success()
               : egl_failure(StatusCode::surface_lost,
                             "EGL could not restore the native context");
}

using ContextProc = void (*)();

[[nodiscard]] inline ContextProc context_proc_address(const char* name) {
    return reinterpret_cast<ContextProc>(eglGetProcAddress(name));
}

[[nodiscard]] inline EGLDisplay surfaceless_display() {
    const auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay != nullptr) {
        const auto display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                                EGL_DEFAULT_DISPLAY, nullptr);
        if (display != EGL_NO_DISPLAY) {
            return display;
        }
    }
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

// Creates the EGL display, pbuffer and context and leaves the context current.
[[nodiscard]] inline Status create_context(Context& native) {
    native.display = surfaceless_display();
    if (native.display == EGL_NO_DISPLAY) {
        return egl_failure(StatusCode::unavailable,
                           "EGL did not expose a native display");
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(native.display, &major, &minor) != EGL_TRUE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL display initialization failed");
    }

#if defined(TRUFFLE_GL_API_DESKTOP)
    constexpr EGLenum api = EGL_OPENGL_API;
    constexpr EGLint renderable = EGL_OPENGL_BIT;
#else
    constexpr EGLenum api = EGL_OPENGL_ES_API;
#ifdef EGL_OPENGL_ES3_BIT
    constexpr EGLint renderable = EGL_OPENGL_ES3_BIT;
#else
    constexpr EGLint renderable = EGL_OPENGL_ES2_BIT;
#endif
#endif
    if (eglBindAPI(api) != EGL_TRUE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL could not bind the requested GL API");
    }
    const std::array<EGLint, 17> configAttributes{
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, renderable,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(native.display, configAttributes.data(), &config, 1,
                        &configCount) != EGL_TRUE ||
        configCount == 0) {
        return egl_failure(StatusCode::unavailable,
                           "EGL found no compatible pbuffer configuration");
    }
    constexpr std::array<EGLint, 5> pbufferAttributes{
        EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
    };
    native.surface = eglCreatePbufferSurface(native.display, config,
                                             pbufferAttributes.data());
    if (native.surface == EGL_NO_SURFACE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL pbuffer creation failed");
    }

#if defined(TRUFFLE_GL_API_DESKTOP)
    const std::array<EGLint, 7> contextAttributes{
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE,
    };
    native.context = eglCreateContext(native.display, config, EGL_NO_CONTEXT,
                                      contextAttributes.data());
    if (native.context == EGL_NO_CONTEXT) {
        native.context = eglCreateContext(native.display, config,
                                          EGL_NO_CONTEXT, nullptr);
    }
#else
    constexpr std::array<EGLint, 3> contextAttributes{
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE,
    };
    native.context = eglCreateContext(native.display, config, EGL_NO_CONTEXT,
                                      contextAttributes.data());
#endif
    if (native.context == EGL_NO_CONTEXT) {
        return egl_failure(StatusCode::unavailable,
                           "EGL context creation failed");
    }
    if (eglMakeCurrent(native.display, native.surface, native.surface,
                       native.context) != EGL_TRUE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL could not make the native context current");
    }
    return Status::success();
}

} // namespace truffle::rhi::detail::gl_probe

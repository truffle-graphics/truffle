#pragma once

#include "foundation_backend.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(TRUFFLE_EGL_API_OPENGL)
#include <GL/gl.h>
#elif defined(TRUFFLE_EGL_API_OPENGLES)
#include <GLES3/gl3.h>
#else
#error "an EGL API profile must be selected"
#endif

#include <array>
#include <memory>
#include <mutex>
#include <string>

namespace truffle::rhi::detail::egl_probe {

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
    std::mutex mutex;
};

struct Probe {
    std::shared_ptr<Context> context;
    std::string adapterName;
};

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

[[nodiscard]] inline Result<Probe> initialize() {
    auto native = std::make_shared<Context>();
    native->display = surfaceless_display();
    if (native->display == EGL_NO_DISPLAY) {
        return egl_failure(StatusCode::unavailable,
                           "EGL did not expose a native display");
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(native->display, &major, &minor) != EGL_TRUE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL display initialization failed");
    }

#if defined(TRUFFLE_EGL_API_OPENGL)
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
    if (eglChooseConfig(native->display, configAttributes.data(), &config, 1,
                        &configCount) != EGL_TRUE ||
        configCount == 0) {
        return egl_failure(StatusCode::unavailable,
                           "EGL found no compatible pbuffer configuration");
    }
    constexpr std::array<EGLint, 5> pbufferAttributes{
        EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
    };
    native->surface = eglCreatePbufferSurface(native->display, config,
                                               pbufferAttributes.data());
    if (native->surface == EGL_NO_SURFACE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL pbuffer creation failed");
    }

#if defined(TRUFFLE_EGL_API_OPENGL)
    const std::array<EGLint, 7> contextAttributes{
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE,
    };
    native->context = eglCreateContext(native->display, config, EGL_NO_CONTEXT,
                                       contextAttributes.data());
    if (native->context == EGL_NO_CONTEXT) {
        native->context = eglCreateContext(native->display, config,
                                           EGL_NO_CONTEXT, nullptr);
    }
#else
    constexpr std::array<EGLint, 3> contextAttributes{
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE,
    };
    native->context = eglCreateContext(native->display, config, EGL_NO_CONTEXT,
                                       contextAttributes.data());
#endif
    if (native->context == EGL_NO_CONTEXT) {
        return egl_failure(StatusCode::unavailable,
                           "EGL context creation failed");
    }
    if (eglMakeCurrent(native->display, native->surface, native->surface,
                       native->context) != EGL_TRUE) {
        return egl_failure(StatusCode::unavailable,
                           "EGL could not make the native context current");
    }

    glViewport(0, 0, 1, 1);
    glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    std::array<unsigned char, 4> pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    if (glGetError() != GL_NO_ERROR || pixel[0] < 60 || pixel[0] > 68 ||
        pixel[1] < 124 || pixel[1] > 132 || pixel[2] < 188 ||
        pixel[2] > 196 || pixel[3] != 255) {
        return Status::failure(
            StatusCode::backend_validation_failed,
            "the native EGL clear/readback smoke result was not deterministic");
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::string name = renderer != nullptr ? renderer : "EGL adapter";
    if (version != nullptr) {
        name += " (";
        name += version;
        name += ')';
    }
    return Probe{.context = std::move(native), .adapterName = std::move(name)};
}

[[nodiscard]] inline Status submit(
    const std::shared_ptr<void>& nativeContext,
    std::span<const NativeCommand> commands,
    std::span<const NativeSemaphorePoint> waits,
    std::span<const NativeSemaphorePoint> signals) {
    if (!commands.empty() || !waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "the EGL matrix slice currently supports empty native smoke submissions only");
    }
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || context->display == EGL_NO_DISPLAY ||
        context->context == EGL_NO_CONTEXT) {
        return Status::failure(StatusCode::device_lost,
                               "the EGL native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    if (eglMakeCurrent(context->display, context->surface, context->surface,
                       context->context) != EGL_TRUE) {
        return egl_failure(StatusCode::surface_lost,
                           "EGL could not restore the native context");
    }
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "the native GL queue smoke reported an error");
}

} // namespace truffle::rhi::detail::egl_probe

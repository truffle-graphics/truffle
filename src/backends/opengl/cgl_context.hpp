#pragma once

// CGL owns the native GL context on macOS. The context is offscreen: it has
// no drawable, so every render and transfer targets an application FBO.

#include "foundation_backend.hpp"
#include "gl_api.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <string>

namespace truffle::rhi::detail::gl_probe {

[[nodiscard]] inline Status cgl_failure(StatusCode code, std::string message,
                                        CGLError error) {
    const char* reason = CGLErrorString(error);
    if (reason != nullptr) {
        message += ": ";
        message += reason;
    }
    BackendDiagnostic detail{
        .domain = "cgl",
        .nativeCode = static_cast<std::int64_t>(error),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(detail));
}

struct Context {
    ~Context() {
        if (context == nullptr) {
            return;
        }
        if (CGLGetCurrentContext() == context) {
            CGLSetCurrentContext(nullptr);
        }
        CGLDestroyContext(context);
    }

    CGLContextObj context = nullptr;
    void (*debugCallback)(const BackendDiagnostic&, void*) = nullptr;
    void* debugUserData = nullptr;
    std::atomic_uint validationErrors{0};
    bool debugOutput = false;
    std::mutex mutex;
};

[[nodiscard]] inline bool context_available(const Context& context) noexcept {
    return context.context != nullptr;
}

[[nodiscard]] inline Status make_current(Context& context) {
    if (CGLGetCurrentContext() == context.context) {
        return Status::success();
    }
    const auto error = CGLSetCurrentContext(context.context);
    return error == kCGLNoError
               ? Status::success()
               : cgl_failure(StatusCode::surface_lost,
                             "CGL could not restore the native context", error);
}

// Creates an accelerated GL 4.1 core context and leaves it current.
[[nodiscard]] inline Status create_context(Context& native) {
    const std::array<CGLPixelFormatAttribute, 9> attributes{
        kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core),
        kCGLPFAAccelerated,
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
        return cgl_failure(StatusCode::unavailable,
                           "CGL found no accelerated GL 4.1 core pixel format",
                           error == kCGLNoError ? kCGLBadPixelFormat : error);
    }
    error = CGLCreateContext(pixelFormat, nullptr, &native.context);
    CGLReleasePixelFormat(pixelFormat);
    if (error != kCGLNoError || native.context == nullptr) {
        native.context = nullptr;
        return cgl_failure(StatusCode::unavailable,
                           "CGL context creation failed",
                           error == kCGLNoError ? kCGLBadContext : error);
    }
    error = CGLSetCurrentContext(native.context);
    if (error != kCGLNoError) {
        return cgl_failure(StatusCode::unavailable,
                           "CGL could not make the native context current",
                           error);
    }
    return Status::success();
}

} // namespace truffle::rhi::detail::gl_probe

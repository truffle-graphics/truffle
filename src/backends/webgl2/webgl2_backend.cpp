#include "truffle/rhi/webgl2_backend.hpp"

#include "foundation_backend.hpp"

#ifdef TRUFFLE_HAS_WEBGL2_NATIVE
#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>
#endif

namespace truffle::rhi {

#ifdef TRUFFLE_HAS_WEBGL2_NATIVE
namespace {

struct WebGlContext {
    ~WebGlContext() {
        if (handle > 0) {
            emscripten_webgl_destroy_context(handle);
        }
    }

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle = 0;
    std::mutex mutex;
};

[[nodiscard]] Status webgl_failure(StatusCode code, std::string message,
                                   EMSCRIPTEN_RESULT result) {
    BackendDiagnostic detail{
        .domain = "webgl2",
        .nativeCode = static_cast<std::int64_t>(result),
        .objectLabel = {},
        .message = message,
    };
    return Status::failure(code, std::move(message), std::move(detail));
}

struct WebGlProbe {
    std::shared_ptr<WebGlContext> context;
    std::string adapterName;
};

[[nodiscard]] Result<WebGlProbe> initialize_webgl2() {
    EmscriptenWebGLContextAttributes attributes{};
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.enableExtensionsByDefault = true;
    auto context = std::make_shared<WebGlContext>();
    context->handle = emscripten_webgl_create_context("#canvas", &attributes);
    if (context->handle <= 0) {
        return webgl_failure(StatusCode::unavailable,
                             "the browser did not expose a WebGL2 canvas context",
                             static_cast<EMSCRIPTEN_RESULT>(context->handle));
    }
    const auto makeCurrent =
        emscripten_webgl_make_context_current(context->handle);
    if (makeCurrent != EMSCRIPTEN_RESULT_SUCCESS) {
        return webgl_failure(StatusCode::unavailable,
                             "the WebGL2 context could not be made current",
                             makeCurrent);
    }
    glViewport(0, 0, 1, 1);
    glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    std::array<unsigned char, 4> pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    if (glGetError() != GL_NO_ERROR) {
        return Status::failure(StatusCode::backend_validation_failed,
                               "the WebGL2 native smoke reported a GL error");
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return WebGlProbe{
        .context = std::move(context),
        .adapterName = renderer != nullptr ? renderer : "WebGL2 adapter",
    };
}

[[nodiscard]] Status submit_webgl2_commands(
    const std::shared_ptr<void>& nativeContext,
    QueueKind,
    std::span<const detail::NativeCommand> commands,
    std::span<const detail::NativeSemaphorePoint> waits,
    std::span<const detail::NativeSemaphorePoint> signals) {
    if (!commands.empty() || !waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "the WebGL2 matrix slice currently supports empty native smoke submissions only");
    }
    const auto context = std::static_pointer_cast<WebGlContext>(nativeContext);
    if (!context || context->handle <= 0) {
        return Status::failure(StatusCode::device_lost,
                               "the WebGL2 native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    const auto makeCurrent =
        emscripten_webgl_make_context_current(context->handle);
    if (makeCurrent != EMSCRIPTEN_RESULT_SUCCESS) {
        return webgl_failure(StatusCode::surface_lost,
                             "the WebGL2 context could not be restored",
                             makeCurrent);
    }
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "the WebGL2 queue smoke reported a GL error");
}

} // namespace
#endif

Result<Instance> create_webgl2_instance(const InstanceDesc& desc) {
#ifdef TRUFFLE_HAS_WEBGL2_NATIVE
    auto probe = initialize_webgl2();
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::webgl2;
    config.platform = PlatformKind::web;
    config.maturity = BackendMaturity::source_only;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.nativeSubmit = &submit_webgl2_commands;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::webgl2, "WebGL2");
#endif
}

} // namespace truffle::rhi

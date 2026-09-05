#pragma once

#include "foundation_backend.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(TRUFFLE_EGL_API_OPENGL)
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#elif defined(TRUFFLE_EGL_API_OPENGLES)
#include <GLES3/gl3.h>
#else
#error "an EGL API profile must be selected"
#endif

#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

[[nodiscard]] inline Status make_current(Context& context) {
    return eglMakeCurrent(context.display, context.surface, context.surface,
                          context.context) == EGL_TRUE
               ? Status::success()
               : egl_failure(StatusCode::surface_lost,
                             "EGL could not restore the native context");
}

struct BufferResource {
    ~BufferResource() {
        if (!context || name == 0) {
            return;
        }
        std::lock_guard contextLock{context->mutex};
        if (make_current(*context).ok()) {
            if (mapped != nullptr) {
                glBindBuffer(GL_COPY_WRITE_BUFFER, name);
                glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            }
            glDeleteBuffers(1, &name);
        }
    }

    std::shared_ptr<Context> context;
    GLuint name = 0;
    std::size_t size = 0;
    MemoryDomain memory = MemoryDomain::device_local;
    void* mapped = nullptr;
    GLenum mappedTarget = GL_COPY_WRITE_BUFFER;
    std::mutex mutex;
};

[[nodiscard]] inline GLenum buffer_usage_hint(MemoryDomain memory) {
    switch (memory) {
    case MemoryDomain::upload:
        return GL_STREAM_DRAW;
    case MemoryDomain::readback:
        return GL_STREAM_READ;
    case MemoryDomain::device_local:
        return GL_STATIC_DRAW;
    case MemoryDomain::external:
        break;
    }
    return GL_STATIC_DRAW;
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_buffer(
    const std::shared_ptr<void>& nativeContext, const BufferDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || context->context == EGL_NO_CONTEXT) {
        return Status::failure(StatusCode::device_lost,
                               "the EGL native context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL external buffers are not implemented");
    }
    auto resource = std::make_shared<BufferResource>();
    resource->context = context;
    resource->size = desc.size;
    resource->memory = desc.memory;
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    glGenBuffers(1, &resource->name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, resource->name);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(desc.size),
                 nullptr, buffer_usage_hint(desc.memory));
    if (desc.mappedAtCreation) {
        const auto flags = desc.memory == MemoryDomain::readback
                               ? GL_MAP_READ_BIT
                               : GL_MAP_WRITE_BIT |
                                     GL_MAP_FLUSH_EXPLICIT_BIT;
        resource->mapped = glMapBufferRange(
            GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(desc.size), flags);
    }
    if (glGetError() != GL_NO_ERROR || resource->name == 0 ||
        (desc.mappedAtCreation && resource->mapped == nullptr)) {
        if (resource->name != 0) {
            glDeleteBuffers(1, &resource->name);
            resource->name = 0;
        }
        return Status::failure(StatusCode::backend_error,
                               "EGL/GL buffer allocation failed");
    }
    return std::static_pointer_cast<void>(std::move(resource));
}

[[nodiscard]] inline Status ensure_mapped(BufferResource& resource,
                                          bool forRead) {
    if (resource.memory == MemoryDomain::device_local) {
        return Status::failure(StatusCode::unsupported,
                               "device-local GL buffers are not host mappable");
    }
    if (resource.mapped != nullptr) {
        return Status::success();
    }
    resource.mappedTarget = forRead ? GL_COPY_READ_BUFFER
                                    : GL_COPY_WRITE_BUFFER;
    glBindBuffer(resource.mappedTarget, resource.name);
    const auto flags = forRead ? GL_MAP_READ_BIT
                               : GL_MAP_WRITE_BIT |
                                     GL_MAP_FLUSH_EXPLICIT_BIT;
    resource.mapped = glMapBufferRange(
        resource.mappedTarget, 0, static_cast<GLsizeiptr>(resource.size), flags);
    return resource.mapped != nullptr && glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_error,
                                 "EGL/GL buffer mapping failed");
}

[[nodiscard]] inline Result<std::span<std::byte>> map_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "EGL/GL buffer resource is invalid");
    }
    std::lock_guard contextLock{resource->context->mutex};
    std::lock_guard resourceLock{resource->mutex};
    if (auto status = make_current(*resource->context); !status.ok()) {
        return status;
    }
    if (auto status = ensure_mapped(
            *resource, resource->memory == MemoryDomain::readback);
        !status.ok()) {
        return status;
    }
    return std::span<std::byte>{static_cast<std::byte*>(resource->mapped),
                                resource->size};
}

[[nodiscard]] inline Status unmap_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (!resource || resource->mapped == nullptr) {
        return Status::failure(StatusCode::invalid_state,
                               "EGL/GL buffer is not mapped");
    }
    std::lock_guard contextLock{resource->context->mutex};
    std::lock_guard resourceLock{resource->mutex};
    if (auto status = make_current(*resource->context); !status.ok()) {
        return status;
    }
    glBindBuffer(resource->mappedTarget, resource->name);
    const auto accepted = glUnmapBuffer(resource->mappedTarget);
    resource->mapped = nullptr;
    return accepted == GL_TRUE && glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_error,
                                 "EGL/GL buffer unmap failed");
}

[[nodiscard]] inline Status buffer_range(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    return resource && offset <= resource->size && size <= resource->size - offset
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "EGL/GL buffer range is invalid");
}

[[nodiscard]] inline Status flush_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (auto status = buffer_range(nativeResource, offset, size); !status.ok()) {
        return status;
    }
    if (resource->memory == MemoryDomain::readback) {
        return Status::success();
    }
    std::lock_guard contextLock{resource->context->mutex};
    std::lock_guard resourceLock{resource->mutex};
    if (auto status = make_current(*resource->context); !status.ok()) {
        return status;
    }
    if (resource->mapped == nullptr) {
        return Status::success();
    }
    glBindBuffer(resource->mappedTarget, resource->name);
    glFlushMappedBufferRange(resource->mappedTarget,
                             static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(size));
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_error,
                                 "EGL/GL buffer flush failed");
}

[[nodiscard]] inline Status write_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<const std::byte> data) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (auto status = buffer_range(nativeResource, offset, data.size());
        !status.ok()) {
        return status;
    }
    std::lock_guard contextLock{resource->context->mutex};
    std::lock_guard resourceLock{resource->mutex};
    if (auto status = make_current(*resource->context); !status.ok()) {
        return status;
    }
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_mapped(*resource, false); !status.ok()) {
        return status;
    }
    std::memcpy(static_cast<std::byte*>(resource->mapped) + offset, data.data(),
                data.size());
    glBindBuffer(resource->mappedTarget, resource->name);
    glFlushMappedBufferRange(resource->mappedTarget,
                             static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(data.size()));
    if (temporary) {
        glUnmapBuffer(resource->mappedTarget);
        resource->mapped = nullptr;
    }
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_error,
                                 "EGL/GL buffer write failed");
}

[[nodiscard]] inline Status read_buffer(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::span<std::byte> data) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (auto status = buffer_range(nativeResource, offset, data.size());
        !status.ok()) {
        return status;
    }
    std::lock_guard contextLock{resource->context->mutex};
    std::lock_guard resourceLock{resource->mutex};
    if (auto status = make_current(*resource->context); !status.ok()) {
        return status;
    }
    const bool temporary = resource->mapped == nullptr;
    if (auto status = ensure_mapped(*resource, true); !status.ok()) {
        return status;
    }
    std::memcpy(data.data(),
                static_cast<const std::byte*>(resource->mapped) + offset,
                data.size());
    if (temporary) {
        glUnmapBuffer(resource->mappedTarget);
        resource->mapped = nullptr;
    }
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_error,
                                 "EGL/GL buffer read failed");
}

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
    QueueKind,
    std::span<const NativeCommand> commands,
    std::span<const NativeSemaphorePoint> waits,
    std::span<const NativeSemaphorePoint> signals) {
    if (!waits.empty() || !signals.empty()) {
        return Status::failure(
            StatusCode::unsupported,
            "EGL/GL timeline semaphore submission is not implemented");
    }
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || context->display == EGL_NO_DISPLAY ||
        context->context == EGL_NO_CONTEXT) {
        return Status::failure(StatusCode::device_lost,
                               "the EGL native context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    for (const auto& command : commands) {
        if (command.kind == NativeCommandKind::barrier) {
            continue;
        }
        if (command.kind != NativeCommandKind::transfer ||
            (command.transfer.kind != NativeTransferKind::copy_buffer &&
             command.transfer.kind != NativeTransferKind::fill_buffer)) {
            return Status::failure(
                StatusCode::unsupported,
                "the EGL/GL resource slice supports buffer transfers only");
        }
    }
    std::vector<GLuint> transients;
    for (const auto& command : commands) {
        if (command.kind == NativeCommandKind::barrier) {
            continue;
        }
        const auto& transfer = command.transfer;
        if (transfer.kind == NativeTransferKind::copy_buffer) {
            const auto source =
                std::static_pointer_cast<BufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<BufferResource>(transfer.destination);
            if (!source || !destination) {
                return Status::failure(StatusCode::invalid_argument,
                                       "EGL/GL buffer copy resources are invalid");
            }
            glBindBuffer(GL_COPY_READ_BUFFER, source->name);
            glBindBuffer(GL_COPY_WRITE_BUFFER, destination->name);
            glCopyBufferSubData(
                GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                static_cast<GLintptr>(transfer.buffer.sourceOffset),
                static_cast<GLintptr>(transfer.buffer.destinationOffset),
                static_cast<GLsizeiptr>(transfer.buffer.size));
            continue;
        }
        const auto destination =
            std::static_pointer_cast<BufferResource>(transfer.destination);
        if (!destination) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL buffer fill resource is invalid");
        }
        std::vector<std::byte> bytes(transfer.buffer.size,
                                     transfer.fillValue);
        GLuint staging = 0;
        glGenBuffers(1, &staging);
        glBindBuffer(GL_COPY_READ_BUFFER, staging);
        glBufferData(GL_COPY_READ_BUFFER,
                     static_cast<GLsizeiptr>(bytes.size()), bytes.data(),
                     GL_STREAM_DRAW);
        glBindBuffer(GL_COPY_WRITE_BUFFER, destination->name);
        glCopyBufferSubData(
            GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0,
            static_cast<GLintptr>(transfer.buffer.destinationOffset),
            static_cast<GLsizeiptr>(transfer.buffer.size));
        transients.push_back(staging);
    }
    glFinish();
    const auto error = glGetError();
    if (!transients.empty()) {
        glDeleteBuffers(static_cast<GLsizei>(transients.size()),
                        transients.data());
    }
    return error == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "the native GL queue reported an error");
}

} // namespace truffle::rhi::detail::egl_probe

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
#include <GLES3/gl3ext.h>
#else
#error "an EGL API profile must be selected"
#endif

#include <array>
#include <algorithm>
#include <atomic>
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
    void (*debugCallback)(const BackendDiagnostic&, void*) = nullptr;
    void* debugUserData = nullptr;
    std::atomic_uint validationErrors{0};
    bool debugOutput = false;
    std::mutex mutex;
};

[[nodiscard]] inline Status make_current(Context& context) {
    return eglMakeCurrent(context.display, context.surface, context.surface,
                          context.context) == EGL_TRUE
               ? Status::success()
               : egl_failure(StatusCode::surface_lost,
                             "EGL could not restore the native context");
}

#if defined(TRUFFLE_EGL_API_OPENGL)
using DebugMessageCallbackProc = PFNGLDEBUGMESSAGECALLBACKPROC;
inline constexpr GLenum debugOutput = GL_DEBUG_OUTPUT;
inline constexpr GLenum debugOutputSynchronous = GL_DEBUG_OUTPUT_SYNCHRONOUS;
inline constexpr GLenum debugTypeError = GL_DEBUG_TYPE_ERROR;
#else
using DebugMessageCallbackProc = PFNGLDEBUGMESSAGECALLBACKKHRPROC;
inline constexpr GLenum debugOutput = GL_DEBUG_OUTPUT_KHR;
inline constexpr GLenum debugOutputSynchronous = GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR;
inline constexpr GLenum debugTypeError = GL_DEBUG_TYPE_ERROR_KHR;
#endif

inline void debug_message(GLenum source, GLenum type, GLuint id,
                          GLenum severity, GLsizei length,
                          const GLchar* message, const void* userData) {
    auto* context = static_cast<Context*>(const_cast<void*>(userData));
    if (context == nullptr || type != debugTypeError) {
        return;
    }
    ++context->validationErrors;
    if (context->debugCallback == nullptr) {
        return;
    }
    const auto messageSize = length > 0 ? static_cast<std::size_t>(length)
                                        : std::strlen(message);
    BackendDiagnostic diagnostic{
        .domain = "opengl",
        .nativeCode = static_cast<std::int64_t>(id),
        .objectLabel = {},
        .message = std::string{message, messageSize},
    };
    (void)source;
    (void)severity;
    context->debugCallback(diagnostic, context->debugUserData);
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

struct TextureFormatInfo {
    GLenum internalFormat = 0;
    GLenum format = 0;
    GLenum type = 0;
    std::uint32_t bytesPerPixel = 0;
    TextureAspect aspects = TextureAspect::none;
};

[[nodiscard]] inline TextureFormatInfo texture_format(TextureFormat format) {
    switch (format) {
    case TextureFormat::r8_unorm:
        return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, TextureAspect::color};
    case TextureFormat::rg8_unorm:
        return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, TextureAspect::color};
    case TextureFormat::rgba8_unorm:
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, TextureAspect::color};
    case TextureFormat::rgba8_srgb:
        return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4,
                TextureAspect::color};
    case TextureFormat::rgba16_float:
        return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, TextureAspect::color};
    case TextureFormat::rgba32_float:
        return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16, TextureAspect::color};
    case TextureFormat::depth16_unorm:
        return {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 2,
                TextureAspect::depth};
    case TextureFormat::depth24_unorm_stencil8:
        return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4,
                TextureAspect::depth | TextureAspect::stencil};
#if defined(TRUFFLE_EGL_API_OPENGL)
    case TextureFormat::depth32_float:
        return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4,
                TextureAspect::depth};
    case TextureFormat::depth32_float_stencil8:
        return {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL,
                GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 8,
                TextureAspect::depth | TextureAspect::stencil};
#endif
    default:
        return {};
    }
}

struct TextureResource {
    ~TextureResource() {
        if (!context || name == 0) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteTextures(1, &name);
        }
    }

    std::shared_ptr<Context> context;
    GLuint name = 0;
    GLenum target = GL_TEXTURE_2D;
    TextureDesc desc;
    TextureFormatInfo format;
    std::shared_ptr<void> parent;
};

[[nodiscard]] inline GLenum texture_target(const TextureDesc& desc) noexcept {
    switch (desc.dimension) {
    case TextureDimension::d2:
#if defined(TRUFFLE_EGL_API_OPENGL)
        if (desc.sampleCount > 1) {
            return desc.arrayLayers == 1 ? GL_TEXTURE_2D_MULTISAMPLE
                                         : GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        }
#endif
        return desc.arrayLayers == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
    case TextureDimension::d3:
        return GL_TEXTURE_3D;
    case TextureDimension::cube:
#if defined(TRUFFLE_EGL_API_OPENGL)
        return desc.arrayLayers == 6 ? GL_TEXTURE_CUBE_MAP
                                     : GL_TEXTURE_CUBE_MAP_ARRAY;
#else
        return desc.arrayLayers == 6 ? GL_TEXTURE_CUBE_MAP : 0;
#endif
    case TextureDimension::d1:
        return 0;
    }
    return 0;
}

struct SamplerResource {
    ~SamplerResource() {
        if (!context || name == 0) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteSamplers(1, &name);
        }
    }

    std::shared_ptr<Context> context;
    GLuint name = 0;
};

[[nodiscard]] inline GLenum sampler_filter(Filter filter) noexcept {
    return filter == Filter::linear ? GL_LINEAR : GL_NEAREST;
}

[[nodiscard]] inline GLenum sampler_address(SamplerAddressMode mode) noexcept {
    switch (mode) {
    case SamplerAddressMode::clamp_to_edge:
        return GL_CLAMP_TO_EDGE;
    case SamplerAddressMode::repeat:
        return GL_REPEAT;
    case SamplerAddressMode::mirror_repeat:
        return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_texture(
    const std::shared_ptr<void>& nativeContext, const TextureDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    const auto format = texture_format(desc.format);
    if (!context || context->context == EGL_NO_CONTEXT) {
        return Status::failure(StatusCode::device_lost,
                               "the EGL native context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL external textures are not implemented");
    }
    const auto target = texture_target(desc);
    if (target == 0 || format.internalFormat == 0
#if defined(TRUFFLE_EGL_API_OPENGLES)
        || desc.sampleCount != 1
#endif
    ) {
        return Status::failure(
            StatusCode::unsupported,
            "this EGL/GL texture shape or format is not implemented");
    }
    auto resource = std::make_shared<TextureResource>();
    resource->context = context;
    resource->desc = desc;
    resource->format = format;
    resource->target = target;
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    glGenTextures(1, &resource->name);
    glBindTexture(resource->target, resource->name);
    if (desc.sampleCount > 1) {
#if defined(TRUFFLE_EGL_API_OPENGL)
        if (desc.arrayLayers == 1) {
            glTexStorage2DMultisample(
                resource->target, static_cast<GLsizei>(desc.sampleCount),
                format.internalFormat, static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height), GL_TRUE);
        } else {
            glTexStorage3DMultisample(
                resource->target, static_cast<GLsizei>(desc.sampleCount),
                format.internalFormat, static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height),
                static_cast<GLsizei>(desc.arrayLayers), GL_TRUE);
        }
#endif
    } else if (desc.dimension == TextureDimension::d3 ||
               (desc.dimension == TextureDimension::d2 &&
                desc.arrayLayers != 1) ||
               (desc.dimension == TextureDimension::cube &&
                desc.arrayLayers != 6)) {
        const auto depth = desc.dimension == TextureDimension::d3
                               ? desc.extent.depth
                               : desc.arrayLayers;
        glTexStorage3D(resource->target, static_cast<GLsizei>(desc.mipLevels),
                       format.internalFormat,
                       static_cast<GLsizei>(desc.extent.width),
                       static_cast<GLsizei>(desc.extent.height),
                       static_cast<GLsizei>(depth));
    } else {
        glTexStorage2D(resource->target, static_cast<GLsizei>(desc.mipLevels),
                       format.internalFormat,
                       static_cast<GLsizei>(desc.extent.width),
                       static_cast<GLsizei>(desc.extent.height));
    }
    if (desc.sampleCount == 1) {
        glTexParameteri(resource->target, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(resource->target, GL_TEXTURE_MAX_LEVEL,
                        static_cast<GLint>(desc.mipLevels - 1));
    }
    if (glGetError() != GL_NO_ERROR || resource->name == 0) {
        if (resource->name != 0) {
            glDeleteTextures(1, &resource->name);
            resource->name = 0;
        }
        return Status::failure(StatusCode::backend_error,
                               "EGL/GL texture allocation failed");
    }
    return std::static_pointer_cast<void>(std::move(resource));
}

#if defined(TRUFFLE_EGL_API_OPENGL)
[[nodiscard]] inline Result<std::shared_ptr<void>> create_texture_view(
    const std::shared_ptr<void>& nativeTexture, const TextureViewDesc& desc) {
    const auto parent = std::static_pointer_cast<TextureResource>(nativeTexture);
    if (!parent || parent->name == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "EGL/GL texture view source is invalid");
    }
    if (parent->desc.sampleCount != 1) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL multisample texture views are disabled");
    }
    const auto format = texture_format(desc.format);
    if (format.internalFormat == 0) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL texture view format is unsupported");
    }
    auto view = std::make_shared<TextureResource>();
    view->context = parent->context;
    view->target = parent->target;
    view->format = format;
    view->desc = parent->desc;
    view->desc.format = desc.format;
    view->desc.mipLevels = desc.range.mipLevelCount;
    view->desc.arrayLayers = desc.range.arrayLayerCount;
    view->parent = parent;
    std::lock_guard lock{parent->context->mutex};
    if (auto status = make_current(*parent->context); !status.ok()) {
        return status;
    }
    glGenTextures(1, &view->name);
    glTextureView(view->name, view->target, parent->name, format.internalFormat,
                  desc.range.baseMipLevel, desc.range.mipLevelCount,
                  desc.range.baseArrayLayer, desc.range.arrayLayerCount);
    if (glGetError() != GL_NO_ERROR || view->name == 0) {
        if (view->name != 0) {
            glDeleteTextures(1, &view->name);
            view->name = 0;
        }
        return Status::failure(StatusCode::backend_error,
                               "EGL/GL texture view creation failed");
    }
    return std::static_pointer_cast<void>(std::move(view));
}
#endif

[[nodiscard]] inline Result<std::shared_ptr<void>> create_sampler(
    const std::shared_ptr<void>& nativeContext, const SamplerDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || context->context == EGL_NO_CONTEXT) {
        return Status::failure(StatusCode::device_lost,
                               "the EGL native context is unavailable");
    }
    if (desc.maxAnisotropy != 1.0F) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL anisotropic sampling is not enabled");
    }
    auto resource = std::make_shared<SamplerResource>();
    resource->context = context;
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    glGenSamplers(1, &resource->name);
    glSamplerParameteri(resource->name, GL_TEXTURE_MIN_FILTER,
                        static_cast<GLint>(sampler_filter(desc.minFilter)));
    glSamplerParameteri(resource->name, GL_TEXTURE_MAG_FILTER,
                        static_cast<GLint>(sampler_filter(desc.magFilter)));
    glSamplerParameteri(resource->name, GL_TEXTURE_WRAP_S,
                        static_cast<GLint>(sampler_address(desc.addressU)));
    glSamplerParameteri(resource->name, GL_TEXTURE_WRAP_T,
                        static_cast<GLint>(sampler_address(desc.addressV)));
    glSamplerParameteri(resource->name, GL_TEXTURE_WRAP_R,
                        static_cast<GLint>(sampler_address(desc.addressW)));
    glSamplerParameterf(resource->name, GL_TEXTURE_MIN_LOD, desc.lodMin);
    glSamplerParameterf(resource->name, GL_TEXTURE_MAX_LOD, desc.lodMax);
    if (glGetError() != GL_NO_ERROR || resource->name == 0) {
        if (resource->name != 0) {
            glDeleteSamplers(1, &resource->name);
            resource->name = 0;
        }
        return Status::failure(StatusCode::backend_error,
                               "EGL/GL sampler allocation failed");
    }
    return std::static_pointer_cast<void>(std::move(resource));
}

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

[[nodiscard]] inline std::uint32_t mip_size(std::uint32_t size,
                                            std::uint32_t mip) noexcept {
    return std::max(1u, size >> mip);
}

[[nodiscard]] inline bool texture_region_supported(
    const TextureResource& texture, const TextureRegion& region) noexcept {
    const auto mipDepth = texture.desc.dimension == TextureDimension::d3
                              ? mip_size(texture.desc.extent.depth,
                                         region.subresource.mipLevel)
                              : 1u;
    return region.subresource.aspect != TextureAspect::none &&
           has_aspect(texture.format.aspects, region.subresource.aspect) &&
           region.subresource.mipLevel < texture.desc.mipLevels &&
           region.subresource.arrayLayer < texture.desc.arrayLayers &&
           region.extent.width != 0 && region.extent.height != 0 &&
           region.extent.depth != 0 &&
           region.origin.x <= mip_size(texture.desc.extent.width,
                                       region.subresource.mipLevel) &&
           region.extent.width <=
               mip_size(texture.desc.extent.width,
                        region.subresource.mipLevel) - region.origin.x &&
           region.origin.y <= mip_size(texture.desc.extent.height,
                                       region.subresource.mipLevel) &&
           region.extent.height <=
               mip_size(texture.desc.extent.height,
                        region.subresource.mipLevel) - region.origin.y &&
           region.origin.z <= mipDepth &&
           region.extent.depth <= mipDepth - region.origin.z &&
           (texture.desc.dimension == TextureDimension::d3 ||
            (region.origin.z == 0 && region.extent.depth == 1));
}

[[nodiscard]] inline bool texture_data_supported(
    const TextureResource& texture, const TextureRegion& region,
    const TextureDataLayout& layout, std::size_t dataSize) noexcept {
    if (!texture_region_supported(texture, region) ||
        texture.format.bytesPerPixel == 0) {
        return false;
    }
    const auto tightRow = static_cast<std::size_t>(region.extent.width) *
                          texture.format.bytesPerPixel;
    const auto rowBytes = layout.bytesPerRow == 0 ? tightRow
                                                   : layout.bytesPerRow;
    if (rowBytes < tightRow ||
        rowBytes % texture.format.bytesPerPixel != 0 ||
        layout.offset > dataSize) {
        return false;
    }
    const auto rowsPerImage = layout.rowsPerImage == 0
                                  ? region.extent.height
                                  : layout.rowsPerImage;
    if (rowsPerImage < region.extent.height) {
        return false;
    }
    const auto imageBytes = rowsPerImage * rowBytes;
    const auto required = static_cast<std::size_t>(region.extent.depth - 1u) *
                              imageBytes +
                          static_cast<std::size_t>(region.extent.height - 1u) *
                              rowBytes + tightRow;
    return required <= dataSize - layout.offset;
}

inline void configure_pixel_store(GLenum alignmentName, GLenum rowLengthName,
                                  GLenum imageHeightName,
                                  std::size_t bytesPerRow,
                                  std::size_t rowsPerImage,
                                  std::uint32_t bytesPerPixel) {
    glPixelStorei(alignmentName, 1);
    glPixelStorei(rowLengthName,
                  bytesPerRow == 0
                      ? 0
                      : static_cast<GLint>(bytesPerRow / bytesPerPixel));
    glPixelStorei(imageHeightName, static_cast<GLint>(rowsPerImage));
}

inline void reset_pixel_store(GLenum alignmentName, GLenum rowLengthName,
                              GLenum imageHeightName) {
    glPixelStorei(imageHeightName, 0);
    glPixelStorei(rowLengthName, 0);
    glPixelStorei(alignmentName, 4);
}

[[nodiscard]] inline Status attach_color_texture(
    GLenum framebufferTarget, const TextureResource& texture,
    const TextureRegion& region) {
    GLenum attachment = GL_COLOR_ATTACHMENT0;
    if (region.subresource.aspect == TextureAspect::depth) {
        attachment = GL_DEPTH_ATTACHMENT;
    } else if (region.subresource.aspect == TextureAspect::stencil) {
        attachment = GL_STENCIL_ATTACHMENT;
    } else if (region.subresource.aspect ==
               (TextureAspect::depth | TextureAspect::stencil)) {
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    }
    const auto mip = static_cast<GLint>(region.subresource.mipLevel);
    if (texture.desc.dimension == TextureDimension::cube &&
        texture.desc.arrayLayers == 6) {
        glFramebufferTexture2D(
            framebufferTarget, attachment,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.subresource.arrayLayer,
            texture.name, mip);
    } else if (texture.desc.dimension == TextureDimension::d3 ||
               texture.desc.arrayLayers > 1) {
        const auto layer = texture.desc.dimension == TextureDimension::d3
                               ? region.origin.z
                               : region.subresource.arrayLayer;
        glFramebufferTextureLayer(framebufferTarget, attachment, texture.name,
                                  mip, static_cast<GLint>(layer));
    } else {
        glFramebufferTexture2D(framebufferTarget, attachment, texture.target,
                               texture.name, mip);
    }
    if (attachment != GL_COLOR_ATTACHMENT0) {
        constexpr GLenum none = GL_NONE;
        if (framebufferTarget == GL_READ_FRAMEBUFFER) {
            glReadBuffer(GL_NONE);
        } else {
            glDrawBuffers(1, &none);
        }
    }
    return glCheckFramebufferStatus(framebufferTarget) ==
                   GL_FRAMEBUFFER_COMPLETE
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "EGL/GL texture framebuffer is incomplete");
}

[[nodiscard]] inline Status write_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<const std::byte> data, const TextureDataLayout& layout) {
    const auto texture =
        std::static_pointer_cast<TextureResource>(nativeResource);
    if (!texture ||
        !texture_data_supported(*texture, region, layout, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "EGL/GL texture write region is invalid");
    }
    if (texture->desc.sampleCount != 1) {
        return Status::failure(
            StatusCode::unsupported,
            "EGL/GL multisample textures cannot be written directly");
    }
    std::lock_guard lock{texture->context->mutex};
    if (auto status = make_current(*texture->context); !status.ok()) {
        return status;
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    texture->format.bytesPerPixel
                              : layout.bytesPerRow;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(texture->target, texture->name);
    const auto rowsPerImage = layout.rowsPerImage == 0
                                  ? region.extent.height
                                  : layout.rowsPerImage;
    configure_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                          GL_UNPACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                          texture->format.bytesPerPixel);
    const auto* source = data.data() + layout.offset;
    if (texture->desc.dimension == TextureDimension::cube &&
        texture->desc.arrayLayers == 6) {
        glTexSubImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.subresource.arrayLayer,
            static_cast<GLint>(region.subresource.mipLevel),
            static_cast<GLint>(region.origin.x),
            static_cast<GLint>(region.origin.y),
            static_cast<GLsizei>(region.extent.width),
            static_cast<GLsizei>(region.extent.height), texture->format.format,
            texture->format.type, source);
    } else if (texture->desc.dimension == TextureDimension::d3 ||
               texture->desc.arrayLayers > 1) {
        const auto z = texture->desc.dimension == TextureDimension::d3
                           ? region.origin.z
                           : region.subresource.arrayLayer;
        const auto depth = texture->desc.dimension == TextureDimension::d3
                               ? region.extent.depth
                               : 1u;
        glTexSubImage3D(
            texture->target, static_cast<GLint>(region.subresource.mipLevel),
            static_cast<GLint>(region.origin.x),
            static_cast<GLint>(region.origin.y), static_cast<GLint>(z),
            static_cast<GLsizei>(region.extent.width),
            static_cast<GLsizei>(region.extent.height),
            static_cast<GLsizei>(depth), texture->format.format,
            texture->format.type, source);
    } else {
        glTexSubImage2D(texture->target,
                        static_cast<GLint>(region.subresource.mipLevel),
                        static_cast<GLint>(region.origin.x),
                        static_cast<GLint>(region.origin.y),
                        static_cast<GLsizei>(region.extent.width),
                        static_cast<GLsizei>(region.extent.height),
                        texture->format.format, texture->format.type, source);
    }
    reset_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                      GL_UNPACK_IMAGE_HEIGHT);
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "EGL/GL texture write reported an error");
}

[[nodiscard]] inline Status read_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<std::byte> data, const TextureDataLayout& layout) {
    const auto texture =
        std::static_pointer_cast<TextureResource>(nativeResource);
    if (!texture ||
        !texture_data_supported(*texture, region, layout, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "EGL/GL texture read region is invalid");
    }
    if (region.extent.depth != 1 || texture->desc.sampleCount != 1) {
        return Status::failure(
            StatusCode::unsupported,
            "EGL/GL host readback supports one single-sample slice at a time");
    }
    std::lock_guard lock{texture->context->mutex};
    if (auto status = make_current(*texture->context); !status.ok()) {
        return status;
    }
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    if (auto status = attach_color_texture(GL_READ_FRAMEBUFFER, *texture, region);
        !status.ok()) {
        glDeleteFramebuffers(1, &framebuffer);
        return status;
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    texture->format.bytesPerPixel
                              : layout.bytesPerRow;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    const auto rowsPerImage = layout.rowsPerImage == 0
                                  ? region.extent.height
                                  : layout.rowsPerImage;
    configure_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                          GL_PACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                          texture->format.bytesPerPixel);
    glReadPixels(static_cast<GLint>(region.origin.x),
                 static_cast<GLint>(region.origin.y),
                 static_cast<GLsizei>(region.extent.width),
                 static_cast<GLsizei>(region.extent.height),
                 texture->format.format, texture->format.type,
                 data.data() + layout.offset);
    reset_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                      GL_PACK_IMAGE_HEIGHT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "EGL/GL texture read reported an error");
}

struct Probe {
    std::shared_ptr<Context> context;
    std::string adapterName;
    bool debugOutput = false;
    bool textureViews = false;
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

[[nodiscard]] inline Result<Probe> initialize(const InstanceDesc& desc) {
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

    native->debugCallback = desc.debugCallback;
    native->debugUserData = desc.debugUserData;
    if (desc.enableValidation) {
#if defined(TRUFFLE_EGL_API_OPENGL)
        constexpr auto callbackName = "glDebugMessageCallback";
#else
        constexpr auto callbackName = "glDebugMessageCallbackKHR";
#endif
        const auto debugCallback = reinterpret_cast<DebugMessageCallbackProc>(
            eglGetProcAddress(callbackName));
        if (debugCallback != nullptr) {
            glEnable(debugOutput);
            glEnable(debugOutputSynchronous);
            debugCallback(&debug_message, native.get());
            native->debugOutput = glGetError() == GL_NO_ERROR;
        }
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
    if (native->debugOutput) {
        name += " [KHR_debug]";
    }
#if defined(TRUFFLE_EGL_API_OPENGL)
    GLint majorVersion = 0;
    GLint minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    const bool textureViews = majorVersion > 4 ||
                              (majorVersion == 4 && minorVersion >= 3);
#else
    constexpr bool textureViews = false;
#endif
    const bool validationDebugOutput =
        desc.enableValidation && native->debugOutput;
    return Probe{.context = std::move(native),
                 .adapterName = std::move(name),
                 .debugOutput = validationDebugOutput,
                 .textureViews = textureViews};
}

[[nodiscard]] inline Status execute_texture_transfer(
    const NativeTransfer& transfer) {
    const auto sourceTexture =
        std::static_pointer_cast<TextureResource>(transfer.source);
    const auto destinationTexture =
        std::static_pointer_cast<TextureResource>(transfer.destination);
    const auto sourceBuffer =
        std::static_pointer_cast<BufferResource>(transfer.source);
    const auto destinationBuffer =
        std::static_pointer_cast<BufferResource>(transfer.destination);

    if (transfer.kind == NativeTransferKind::copy_buffer_to_texture) {
        if (!sourceBuffer || !destinationTexture ||
            !texture_region_supported(*destinationTexture,
                                      transfer.bufferTexture.texture)) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL buffer-to-texture resources are invalid");
        }
        const auto& region = transfer.bufferTexture.texture;
        const auto& layout = transfer.bufferTexture.layout;
        const auto rowBytes = layout.bytesPerRow == 0
                                  ? static_cast<std::size_t>(region.extent.width) *
                                        destinationTexture->format.bytesPerPixel
                                  : layout.bytesPerRow;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sourceBuffer->name);
        glBindTexture(destinationTexture->target, destinationTexture->name);
        const auto rowsPerImage = layout.rowsPerImage == 0
                                      ? region.extent.height
                                      : layout.rowsPerImage;
        configure_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                              GL_UNPACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                              destinationTexture->format.bytesPerPixel);
        const auto offset = transfer.bufferTexture.bufferOffset + layout.offset;
        const auto* source = reinterpret_cast<const void*>(offset);
        if (destinationTexture->desc.dimension == TextureDimension::cube &&
            destinationTexture->desc.arrayLayers == 6) {
            glTexSubImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X +
                    region.subresource.arrayLayer,
                static_cast<GLint>(region.subresource.mipLevel),
                static_cast<GLint>(region.origin.x),
                static_cast<GLint>(region.origin.y),
                static_cast<GLsizei>(region.extent.width),
                static_cast<GLsizei>(region.extent.height),
                destinationTexture->format.format,
                destinationTexture->format.type, source);
        } else if (destinationTexture->desc.dimension == TextureDimension::d3 ||
                   destinationTexture->desc.arrayLayers > 1) {
            const auto z = destinationTexture->desc.dimension ==
                                   TextureDimension::d3
                               ? region.origin.z
                               : region.subresource.arrayLayer;
            const auto depth = destinationTexture->desc.dimension ==
                                       TextureDimension::d3
                                   ? region.extent.depth
                                   : 1u;
            glTexSubImage3D(
                destinationTexture->target,
                static_cast<GLint>(region.subresource.mipLevel),
                static_cast<GLint>(region.origin.x),
                static_cast<GLint>(region.origin.y), static_cast<GLint>(z),
                static_cast<GLsizei>(region.extent.width),
                static_cast<GLsizei>(region.extent.height),
                static_cast<GLsizei>(depth),
                destinationTexture->format.format,
                destinationTexture->format.type, source);
        } else {
            glTexSubImage2D(
                destinationTexture->target,
                static_cast<GLint>(region.subresource.mipLevel),
                static_cast<GLint>(region.origin.x),
                static_cast<GLint>(region.origin.y),
                static_cast<GLsizei>(region.extent.width),
                static_cast<GLsizei>(region.extent.height),
                destinationTexture->format.format,
                destinationTexture->format.type, source);
        }
        reset_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                          GL_UNPACK_IMAGE_HEIGHT);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    } else if (transfer.kind == NativeTransferKind::copy_texture_to_buffer) {
        if (!sourceTexture || !destinationBuffer ||
            !texture_region_supported(*sourceTexture,
                                      transfer.bufferTexture.texture) ||
            transfer.bufferTexture.texture.extent.depth != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL texture-to-buffer resources are invalid");
        }
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        if (auto status = attach_color_texture(
                GL_READ_FRAMEBUFFER, *sourceTexture,
                transfer.bufferTexture.texture);
            !status.ok()) {
            glDeleteFramebuffers(1, &framebuffer);
            return status;
        }
        const auto& region = transfer.bufferTexture.texture;
        const auto& layout = transfer.bufferTexture.layout;
        const auto rowBytes = layout.bytesPerRow == 0
                                  ? static_cast<std::size_t>(region.extent.width) *
                                        sourceTexture->format.bytesPerPixel
                                  : layout.bytesPerRow;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, destinationBuffer->name);
        const auto rowsPerImage = layout.rowsPerImage == 0
                                      ? region.extent.height
                                      : layout.rowsPerImage;
        configure_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                              GL_PACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                              sourceTexture->format.bytesPerPixel);
        const auto offset = transfer.bufferTexture.bufferOffset + layout.offset;
        glReadPixels(static_cast<GLint>(region.origin.x),
                     static_cast<GLint>(region.origin.y),
                     static_cast<GLsizei>(region.extent.width),
                     static_cast<GLsizei>(region.extent.height),
                     sourceTexture->format.format, sourceTexture->format.type,
                     reinterpret_cast<void*>(offset));
        reset_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                          GL_PACK_IMAGE_HEIGHT);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
    } else if (transfer.kind == NativeTransferKind::clear_texture) {
        if (!destinationTexture ||
            !texture_region_supported(*destinationTexture,
                                      transfer.texture.destination) ||
            transfer.texture.destination.extent.depth != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL texture clear resource is invalid");
        }
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        if (auto status = attach_color_texture(
                GL_DRAW_FRAMEBUFFER, *destinationTexture,
                transfer.texture.destination);
            !status.ok()) {
            glDeleteFramebuffers(1, &framebuffer);
            return status;
        }
        const auto& region = transfer.texture.destination;
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(region.origin.x),
                  static_cast<GLint>(region.origin.y),
                  static_cast<GLsizei>(region.extent.width),
                  static_cast<GLsizei>(region.extent.height));
        if (region.subresource.aspect == TextureAspect::color) {
            const std::array value{transfer.clear.color.r,
                                   transfer.clear.color.g,
                                   transfer.clear.color.b,
                                   transfer.clear.color.a};
            glClearBufferfv(GL_COLOR, 0, value.data());
        } else {
            if (has_aspect(region.subresource.aspect, TextureAspect::depth)) {
                glClearBufferfv(GL_DEPTH, 0, &transfer.clear.depth);
            }
            if (has_aspect(region.subresource.aspect, TextureAspect::stencil)) {
                const auto stencil =
                    static_cast<GLint>(transfer.clear.stencil);
                glClearBufferiv(GL_STENCIL, 0, &stencil);
            }
        }
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
    } else if (transfer.kind == NativeTransferKind::copy_texture ||
               transfer.kind == NativeTransferKind::resolve_texture ||
               transfer.kind == NativeTransferKind::blit_texture) {
        if (!sourceTexture || !destinationTexture) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL texture copy resources are invalid");
        }
        const auto& source = transfer.kind == NativeTransferKind::blit_texture
                                 ? transfer.blit.source
                                 : transfer.texture.source;
        const auto& destination =
            transfer.kind == NativeTransferKind::blit_texture
                ? transfer.blit.destination
                : transfer.texture.destination;
        if (!texture_region_supported(*sourceTexture, source) ||
            !texture_region_supported(*destinationTexture, destination) ||
            source.extent.depth != 1 || destination.extent.depth != 1 ||
            source.subresource.aspect != destination.subresource.aspect) {
            return Status::failure(StatusCode::invalid_argument,
                                   "EGL/GL texture copy regions are invalid");
        }
        std::array<GLuint, 2> framebuffers{};
        glGenFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                          framebuffers.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
        auto status = attach_color_texture(GL_READ_FRAMEBUFFER, *sourceTexture,
                                           source);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
        if (status.ok()) {
            status = attach_color_texture(GL_DRAW_FRAMEBUFFER,
                                          *destinationTexture, destination);
        }
        if (!status.ok()) {
            glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                                 framebuffers.data());
            return status;
        }
        const auto filter =
            transfer.kind == NativeTransferKind::blit_texture &&
                    transfer.blit.filter == Filter::linear
                ? GL_LINEAR
                : GL_NEAREST;
        GLbitfield mask = GL_COLOR_BUFFER_BIT;
        if (has_aspect(source.subresource.aspect, TextureAspect::depth)) {
            mask = GL_DEPTH_BUFFER_BIT;
        }
        if (has_aspect(source.subresource.aspect, TextureAspect::stencil)) {
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        glBlitFramebuffer(
            static_cast<GLint>(source.origin.x),
            static_cast<GLint>(source.origin.y),
            static_cast<GLint>(source.origin.x + source.extent.width),
            static_cast<GLint>(source.origin.y + source.extent.height),
            static_cast<GLint>(destination.origin.x),
            static_cast<GLint>(destination.origin.y),
            static_cast<GLint>(destination.origin.x + destination.extent.width),
            static_cast<GLint>(destination.origin.y + destination.extent.height),
            mask, filter);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()),
                             framebuffers.data());
    } else {
        return Status::failure(StatusCode::unsupported,
                               "this EGL/GL texture transfer is unsupported");
    }
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "the native GL texture transfer reported an error");
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
        if (command.kind != NativeCommandKind::transfer) {
            return Status::failure(
                StatusCode::unsupported,
                "the EGL/GL resource slice supports transfer commands only");
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
        if (transfer.kind != NativeTransferKind::fill_buffer) {
            if (auto status = execute_texture_transfer(transfer);
                !status.ok()) {
                if (!transients.empty()) {
                    glDeleteBuffers(static_cast<GLsizei>(transients.size()),
                                    transients.data());
                }
                return status;
            }
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

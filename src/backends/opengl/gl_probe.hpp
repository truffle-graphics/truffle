#pragma once

#include "foundation_backend.hpp"

#include "gl_api.hpp"
#if defined(TRUFFLE_GL_CONTEXT_CGL)
#include "cgl_context.hpp"
#else
#include "egl_context.hpp"
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gl_graphics.hpp"
#include "gl_program.hpp"
#include "gl_sync.hpp"

namespace truffle::rhi::detail::gl_probe {

#if defined(TRUFFLE_GL_API_DESKTOP)
inline constexpr GLenum packImageHeight = GL_PACK_IMAGE_HEIGHT;
inline constexpr GLenum stencilIndex = GL_STENCIL_INDEX;
#else
inline constexpr GLenum packImageHeight = 0;
inline constexpr GLenum stencilIndex = 0x1901;
#endif

#if defined(TRUFFLE_GL_HAS_DEBUG_OUTPUT)
#if defined(TRUFFLE_GL_API_DESKTOP)
inline constexpr GLenum debugOutput = GL_DEBUG_OUTPUT;
inline constexpr GLenum debugOutputSynchronous = GL_DEBUG_OUTPUT_SYNCHRONOUS;
inline constexpr GLenum debugTypeError = GL_DEBUG_TYPE_ERROR;
#else
inline constexpr GLenum debugOutput = 0x92E0;
inline constexpr GLenum debugOutputSynchronous = 0x8242;
inline constexpr GLenum debugTypeError = 0x824C;
#endif

using DebugMessageFunction = void (*)(GLenum, GLenum, GLuint, GLenum, GLsizei,
                                      const GLchar*, const void*);
using DebugMessageCallbackProc = void (*)(DebugMessageFunction, const void*);

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
#endif

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
#if defined(TRUFFLE_GL_API_DESKTOP)
    // Desktop GL stores BGRA texels as RGBA8 and swizzles GL_BGRA transfers.
    case TextureFormat::bgra8_unorm:
        return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4, TextureAspect::color};
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

[[nodiscard]] inline TextureFormatInfo transfer_format(
    const TextureFormatInfo& format, TextureAspect aspect) noexcept {
    auto result = format;
    if (has_aspect(format.aspects, TextureAspect::depth) &&
        has_aspect(format.aspects, TextureAspect::stencil)) {
        if (aspect == TextureAspect::depth) {
            result.format = GL_DEPTH_COMPONENT;
            result.type = GL_FLOAT;
            result.bytesPerPixel = sizeof(float);
        } else if (aspect == TextureAspect::stencil) {
            result.format = stencilIndex;
            result.type = GL_UNSIGNED_BYTE;
            result.bytesPerPixel = 1;
        }
    }
    return result;
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
#if defined(TRUFFLE_GL_API_DESKTOP)
        if (desc.sampleCount > 1) {
            return desc.arrayLayers == 1 ? GL_TEXTURE_2D_MULTISAMPLE
                                         : GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        }
#endif
        return desc.arrayLayers == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
    case TextureDimension::d3:
        return GL_TEXTURE_3D;
    case TextureDimension::cube:
#if defined(TRUFFLE_GL_API_DESKTOP)
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
        if (!context || (names[0] == 0 && names[1] == 0)) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteSamplers(static_cast<GLsizei>(names.size()), names.data());
        }
    }

    std::shared_ptr<Context> context;
    // A single RHI sampler may serve ordinary and comparison uniforms together.
    std::array<GLuint, 2> names{};
};

struct ShaderResource {
    ~ShaderResource() {
        if (!context || name == 0) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteShader(name);
        }
    }

    std::shared_ptr<Context> context;
    GLuint name = 0;
    ShaderDesc desc;
};

struct PipelineResource {
    ~PipelineResource() {
        if (!context || program == 0) {
            return;
        }
        std::lock_guard lock{context->mutex};
        if (make_current(*context).ok()) {
            glDeleteProgram(program);
        }
    }

    std::shared_ptr<Context> context;
    GLuint program = 0;
    PipelineDesc graphics;
    ComputePipelineDesc compute;
    bool isCompute = false;
    program::Layout bindings;
};

[[nodiscard]] inline GLenum shader_stage(ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex:
        return GL_VERTEX_SHADER;
    case ShaderStage::fragment:
        return GL_FRAGMENT_SHADER;
    case ShaderStage::compute:
#if defined(TRUFFLE_GL_HAS_COMPUTE)
        return GL_COMPUTE_SHADER;
#else
        return 0;
#endif
    }
    return 0;
}

[[nodiscard]] inline std::string shader_log(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(0, length)), '\0');
    if (length > 0) {
        glGetShaderInfoLog(shader, length, nullptr, log.data());
    }
    while (!log.empty() && log.back() == '\0') {
        log.pop_back();
    }
    return log;
}

[[nodiscard]] inline std::string program_log(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(0, length)), '\0');
    if (length > 0) {
        glGetProgramInfoLog(program, length, nullptr, log.data());
    }
    while (!log.empty() && log.back() == '\0') {
        log.pop_back();
    }
    return log;
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_shader(
    const std::shared_ptr<void>& nativeContext, const ShaderDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (desc.format != ShaderByteFormat::native_source ||
        desc.entryPoint != "main" || desc.code.empty() ||
        desc.code.size() > static_cast<std::size_t>(
                               std::numeric_limits<GLint>::max())) {
        return Status::failure(
            StatusCode::unsupported,
            "GL shaders require bounded native GLSL with entry point main");
    }
    const auto stage = shader_stage(desc.stage);
    if (stage == 0) {
        return Status::failure(StatusCode::unsupported,
                               "the GL shader stage is unsupported");
    }
    auto shader = std::make_shared<ShaderResource>();
    shader->context = context;
    shader->desc = desc;
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    shader->name = glCreateShader(stage);
    const auto* source = reinterpret_cast<const GLchar*>(desc.code.data());
    const auto length = static_cast<GLint>(desc.code.size());
    glShaderSource(shader->name, 1, &source, &length);
    glCompileShader(shader->name);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader->name, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        const auto log = shader_log(shader->name);
        glDeleteShader(shader->name);
        shader->name = 0;
        return Status::failure(
            StatusCode::backend_error,
            log.empty() ? "GL shader compilation failed" : log);
    }
    return std::static_pointer_cast<void>(std::move(shader));
}

[[nodiscard]] inline Result<GLuint> link_program(
    const std::shared_ptr<Context>& context,
    std::span<const std::shared_ptr<ShaderResource>> shaders) {
    const auto program = glCreateProgram();
    for (const auto& shader : shaders) {
        if (!shader || shader->context != context || shader->name == 0) {
            glDeleteProgram(program);
            return Status::failure(StatusCode::invalid_argument,
                                   "GL pipeline shader is invalid");
        }
        glAttachShader(program, shader->name);
    }
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        const auto log = program_log(program);
        glDeleteProgram(program);
        return Status::failure(
            StatusCode::backend_error,
            log.empty() ? "GL program link failed" : log);
    }
    return program;
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_pipeline(
    const std::shared_ptr<void>& nativeContext, const PipelineDesc& desc,
    const NativePipelineLayout& layout, const std::shared_ptr<void>& nativeVertex,
    const std::shared_ptr<void>& nativeFragment) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    const auto vertex = std::static_pointer_cast<ShaderResource>(nativeVertex);
    const auto fragment =
        std::static_pointer_cast<ShaderResource>(nativeFragment);
    if (!context || !vertex || !fragment || vertex->desc.stage != ShaderStage::vertex ||
        fragment->desc.stage != ShaderStage::fragment) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL graphics shaders are invalid");
    }
    if (auto status = graphics::validate(desc); !status.ok()) {
        return status;
    }
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    const std::array shaders{vertex, fragment};
    auto linked = link_program(context, shaders);
    if (!linked.ok()) {
        return linked.status();
    }
    auto pipeline = std::make_shared<PipelineResource>();
    pipeline->context = context;
    pipeline->program = linked.value();
    const std::array<const ShaderDesc*, 2> descriptions{&vertex->desc, &fragment->desc};
    auto bindings = program::configure(pipeline->program, descriptions, layout,
                                        desc.specializationConstants);
    if (!bindings.ok()) {
        glUseProgram(0);
        glDeleteProgram(pipeline->program);
        pipeline->program = 0;
        return bindings.status();
    }
    pipeline->bindings = std::move(bindings).value();
    pipeline->graphics = desc;
    pipeline->graphics.vertexShader = nullptr;
    pipeline->graphics.fragmentShader = nullptr;
    pipeline->graphics.layout = nullptr;
    pipeline->graphics.cache = nullptr;
    return std::static_pointer_cast<void>(std::move(pipeline));
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_compute_pipeline(
    const std::shared_ptr<void>& nativeContext, const ComputePipelineDesc& desc,
    const NativePipelineLayout& layout, const std::shared_ptr<void>& nativeShader) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    const auto shader = std::static_pointer_cast<ShaderResource>(nativeShader);
    if (!context || !shader || shader->desc.stage != ShaderStage::compute) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL compute shader is invalid");
    }
    if (std::any_of(shader->desc.workgroupSpecializationIds.begin(),
                    shader->desc.workgroupSpecializationIds.end(),
                    [](const auto& id) { return id.has_value(); })) {
        return Status::failure(StatusCode::unsupported,
            "GL native-source workgroup specialization requires compile-time variants, not uniform overrides");
    }
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    const std::array shaders{shader};
    auto linked = link_program(context, shaders);
    if (!linked.ok()) {
        return linked.status();
    }
    auto pipeline = std::make_shared<PipelineResource>();
    pipeline->context = context;
    pipeline->program = linked.value();
    const std::array<const ShaderDesc*, 1> descriptions{&shader->desc};
    auto bindings = program::configure(pipeline->program, descriptions, layout,
                                        desc.specializationConstants);
    if (!bindings.ok()) {
        glUseProgram(0);
        glDeleteProgram(pipeline->program);
        pipeline->program = 0;
        return bindings.status();
    }
    pipeline->bindings = std::move(bindings).value();
#if defined(TRUFFLE_GL_HAS_COMPUTE)
    std::array<GLint, 3> workgroup{};
    glGetProgramiv(pipeline->program, GL_COMPUTE_WORK_GROUP_SIZE, workgroup.data());
    if (workgroup[0] != static_cast<GLint>(desc.requiredWorkgroupSize.width) ||
        workgroup[1] != static_cast<GLint>(desc.requiredWorkgroupSize.height) ||
        workgroup[2] != static_cast<GLint>(desc.requiredWorkgroupSize.depth)) {
        glDeleteProgram(pipeline->program);
        pipeline->program = 0;
        return program::mismatch("GL compute local size disagrees with reflection");
    }
#endif
    pipeline->compute = desc;
    pipeline->compute.computeShader = nullptr;
    pipeline->compute.layout = nullptr;
    pipeline->compute.cache = nullptr;
    pipeline->isCompute = true;
    return std::static_pointer_cast<void>(std::move(pipeline));
}

[[nodiscard]] inline GLenum sampler_filter(Filter filter) noexcept {
    return filter == Filter::linear ? GL_LINEAR : GL_NEAREST;
}

[[nodiscard]] inline Status bind_resources(
    const program::Layout& layout, std::span<const NativeBindingResource> resources) {
    for (const auto& binding : layout.bindings) {
        const auto found = std::find_if(resources.begin(), resources.end(),
            [&](const NativeBindingResource& resource) {
                return resource.group == binding.group &&
                       resource.binding == binding.binding &&
                       resource.arrayElement == binding.element &&
                       resource.type == binding.type;
            });
        if (found == resources.end() || !found->resource) {
            return Status::failure(StatusCode::invalid_state,
                                   "GL draw or dispatch is missing a reflected binding");
        }
        if (binding.type == BindingType::uniform_buffer ||
            binding.type == BindingType::storage_buffer) {
            const auto buffer = std::static_pointer_cast<BufferResource>(found->resource);
            if (found->offset >= buffer->size) {
                return Status::failure(StatusCode::invalid_argument,
                                       "GL buffer binding offset is out of range");
            }
            const auto size = found->size == whole_size
                                  ? buffer->size - found->offset : found->size;
            if (size < binding.minimumSize || size > buffer->size - found->offset) {
                return Status::failure(StatusCode::invalid_argument,
                                       "GL buffer binding size disagrees with reflection");
            }
            GLenum target = GL_UNIFORM_BUFFER;
#if defined(TRUFFLE_GL_HAS_COMPUTE)
            if (binding.type == BindingType::storage_buffer) target = GL_SHADER_STORAGE_BUFFER;
#endif
            glBindBufferRange(target, binding.slot, buffer->name,
                               static_cast<GLintptr>(found->offset),
                               static_cast<GLsizeiptr>(size));
        } else if (binding.type == BindingType::sampled_texture) {
            const auto texture = std::static_pointer_cast<TextureResource>(found->resource);
            GLenum expected = GL_TEXTURE_2D;
            switch (binding.uniformType) {
            case GL_SAMPLER_3D: expected = GL_TEXTURE_3D; break;
            case GL_SAMPLER_CUBE:
            case GL_SAMPLER_CUBE_SHADOW: expected = GL_TEXTURE_CUBE_MAP; break;
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_2D_ARRAY_SHADOW: expected = GL_TEXTURE_2D_ARRAY; break;
            default: break;
            }
            if (texture->target != expected) {
                return Status::failure(StatusCode::invalid_argument,
                                       "GL texture shape disagrees with its sampler uniform");
            }
            glActiveTexture(GL_TEXTURE0 + binding.slot);
            glBindTexture(texture->target, texture->name);
        } else if (binding.type == BindingType::sampler) {
            const auto sampler = std::static_pointer_cast<SamplerResource>(found->resource);
            const bool shadow = binding.uniformType == GL_SAMPLER_2D_SHADOW ||
                                binding.uniformType == GL_SAMPLER_CUBE_SHADOW ||
                                binding.uniformType == GL_SAMPLER_2D_ARRAY_SHADOW;
            glBindSampler(binding.slot, sampler->names[shadow ? 1 : 0]);
        }
    }
    return Status::success();
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

[[nodiscard]] inline GLsizei mip_extent(std::uint32_t extent,
                                       std::uint32_t level) noexcept {
    return static_cast<GLsizei>(std::max(1u, extent >> level));
}

// Allocates every level of the bound texture. GL 4.1 lacks immutable storage,
// so the CGL path specifies each level and face explicitly with null data.
inline void allocate_texture(const TextureResource& resource) {
    const auto& desc = resource.desc;
    const auto& format = resource.format;
    if (desc.sampleCount > 1) {
#if defined(TRUFFLE_GL_API_DESKTOP) && defined(TRUFFLE_GL_HAS_TEXTURE_STORAGE)
        if (desc.arrayLayers == 1) {
            glTexStorage2DMultisample(
                resource.target, static_cast<GLsizei>(desc.sampleCount),
                format.internalFormat, static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height), GL_TRUE);
        } else {
            glTexStorage3DMultisample(
                resource.target, static_cast<GLsizei>(desc.sampleCount),
                format.internalFormat, static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height),
                static_cast<GLsizei>(desc.arrayLayers), GL_TRUE);
        }
#elif defined(TRUFFLE_GL_API_DESKTOP)
        if (desc.arrayLayers == 1) {
            glTexImage2DMultisample(
                resource.target, static_cast<GLsizei>(desc.sampleCount),
                static_cast<GLint>(format.internalFormat),
                static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height), GL_TRUE);
        } else {
            glTexImage3DMultisample(
                resource.target, static_cast<GLsizei>(desc.sampleCount),
                static_cast<GLint>(format.internalFormat),
                static_cast<GLsizei>(desc.extent.width),
                static_cast<GLsizei>(desc.extent.height),
                static_cast<GLsizei>(desc.arrayLayers), GL_TRUE);
        }
#endif
        return;
    }
    const bool layered = desc.dimension == TextureDimension::d3 ||
                         (desc.dimension == TextureDimension::d2 &&
                          desc.arrayLayers != 1) ||
                         (desc.dimension == TextureDimension::cube &&
                          desc.arrayLayers != 6);
#if defined(TRUFFLE_GL_HAS_TEXTURE_STORAGE)
    if (layered) {
        const auto depth = desc.dimension == TextureDimension::d3
                               ? desc.extent.depth
                               : desc.arrayLayers;
        glTexStorage3D(resource.target, static_cast<GLsizei>(desc.mipLevels),
                       format.internalFormat,
                       static_cast<GLsizei>(desc.extent.width),
                       static_cast<GLsizei>(desc.extent.height),
                       static_cast<GLsizei>(depth));
    } else {
        glTexStorage2D(resource.target, static_cast<GLsizei>(desc.mipLevels),
                       format.internalFormat,
                       static_cast<GLsizei>(desc.extent.width),
                       static_cast<GLsizei>(desc.extent.height));
    }
#else
    for (std::uint32_t level = 0; level < desc.mipLevels; ++level) {
        const auto width = mip_extent(desc.extent.width, level);
        const auto height = mip_extent(desc.extent.height, level);
        const auto mip = static_cast<GLint>(level);
        const auto internalFormat = static_cast<GLint>(format.internalFormat);
        if (layered) {
            const auto depth = desc.dimension == TextureDimension::d3
                                   ? mip_extent(desc.extent.depth, level)
                                   : static_cast<GLsizei>(desc.arrayLayers);
            glTexImage3D(resource.target, mip, internalFormat, width, height,
                         depth, 0, format.format, format.type, nullptr);
        } else if (resource.target == GL_TEXTURE_CUBE_MAP) {
            for (GLenum face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip,
                             internalFormat, width, height, 0, format.format,
                             format.type, nullptr);
            }
        } else {
            glTexImage2D(resource.target, mip, internalFormat, width, height, 0,
                         format.format, format.type, nullptr);
        }
    }
#endif
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_texture(
    const std::shared_ptr<void>& nativeContext, const TextureDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    const auto format = texture_format(desc.format);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(StatusCode::unsupported,
                               "GL external textures are not implemented");
    }
    const auto target = texture_target(desc);
    if (target == 0 || format.internalFormat == 0
#if defined(TRUFFLE_GL_API_ES)
        || desc.sampleCount != 1
#endif
    ) {
        return Status::failure(
            StatusCode::unsupported,
            "this GL texture shape or format is not implemented");
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
    allocate_texture(*resource);
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
                               "GL texture allocation failed");
    }
    return std::static_pointer_cast<void>(std::move(resource));
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_texture_view(
    const std::shared_ptr<void>& nativeTexture, const TextureViewDesc& desc) {
    const auto parent = std::static_pointer_cast<TextureResource>(nativeTexture);
    if (!parent || parent->name == 0) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL texture view source is invalid");
    }
    if (parent->desc.sampleCount != 1) {
        return Status::failure(StatusCode::unsupported,
                               "GL multisample texture views are disabled");
    }
#if !defined(TRUFFLE_GL_HAS_TEXTURE_VIEWS)
    if (desc.dimension != parent->desc.dimension || desc.format != parent->desc.format ||
        desc.range.baseMipLevel != 0 || desc.range.mipLevelCount != parent->desc.mipLevels ||
        desc.range.baseArrayLayer != 0 ||
        desc.range.arrayLayerCount != parent->desc.arrayLayers ||
        desc.range.aspects != parent->format.aspects) {
        return Status::failure(StatusCode::unsupported,
                               "this GL context supports full identity texture views only");
    }
    return nativeTexture;
#else
    const auto format = texture_format(desc.format);
    if (format.internalFormat == 0) {
        return Status::failure(StatusCode::unsupported,
                               "GL texture view format is unsupported");
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
                               "GL texture view creation failed");
    }
    return std::static_pointer_cast<void>(std::move(view));
#endif
}

[[nodiscard]] inline Result<std::shared_ptr<void>> create_sampler(
    const std::shared_ptr<void>& nativeContext, const SamplerDesc& desc) {
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (desc.maxAnisotropy != 1.0F) {
        return Status::failure(StatusCode::unsupported,
                               "GL anisotropic sampling is not enabled");
    }
    auto resource = std::make_shared<SamplerResource>();
    resource->context = context;
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    glGenSamplers(static_cast<GLsizei>(resource->names.size()), resource->names.data());
    for (std::size_t index = 0; index < resource->names.size(); ++index) {
        const auto name = resource->names[index];
        glSamplerParameteri(name, GL_TEXTURE_MIN_FILTER,
                             static_cast<GLint>(
                                 desc.minFilter == Filter::linear
                                     ? (desc.mipFilter == Filter::linear
                                            ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST)
                                     : (desc.mipFilter == Filter::linear
                                            ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)));
        glSamplerParameteri(name, GL_TEXTURE_MAG_FILTER,
                             static_cast<GLint>(sampler_filter(desc.magFilter)));
        glSamplerParameteri(name, GL_TEXTURE_WRAP_S,
                             static_cast<GLint>(sampler_address(desc.addressU)));
        glSamplerParameteri(name, GL_TEXTURE_WRAP_T,
                             static_cast<GLint>(sampler_address(desc.addressV)));
        glSamplerParameteri(name, GL_TEXTURE_WRAP_R,
                             static_cast<GLint>(sampler_address(desc.addressW)));
        glSamplerParameterf(name, GL_TEXTURE_MIN_LOD, desc.lodMin);
        glSamplerParameterf(name, GL_TEXTURE_MAX_LOD, desc.lodMax);
        glSamplerParameteri(name, GL_TEXTURE_COMPARE_MODE,
                             index == 1 ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glSamplerParameteri(name, GL_TEXTURE_COMPARE_FUNC,
                             static_cast<GLint>(graphics::compare(desc.compare)));
    }
    if (glGetError() != GL_NO_ERROR ||
        resource->names[0] == 0 || resource->names[1] == 0) {
        glDeleteSamplers(static_cast<GLsizei>(resource->names.size()), resource->names.data());
        resource->names = {};
        return Status::failure(StatusCode::backend_error,
                               "GL sampler allocation failed");
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
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    if (desc.memory == MemoryDomain::external || desc.shareable) {
        return Status::failure(StatusCode::unsupported,
                               "GL external buffers are not implemented");
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
                               "GL buffer allocation failed");
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
                                 "GL buffer mapping failed");
}

[[nodiscard]] inline Result<std::span<std::byte>> map_buffer(
    const std::shared_ptr<void>& nativeResource) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    if (!resource) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL buffer resource is invalid");
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
                               "GL buffer is not mapped");
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
                                 "GL buffer unmap failed");
}

[[nodiscard]] inline Status buffer_range(
    const std::shared_ptr<void>& nativeResource, std::size_t offset,
    std::size_t size) {
    const auto resource = std::static_pointer_cast<BufferResource>(nativeResource);
    return resource && offset <= resource->size && size <= resource->size - offset
               ? Status::success()
               : Status::failure(StatusCode::invalid_argument,
                                 "GL buffer range is invalid");
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
                                 "GL buffer flush failed");
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
                                 "GL buffer write failed");
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
                                 "GL buffer read failed");
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
    const auto transfer = transfer_format(texture.format,
                                          region.subresource.aspect);
    const auto tightRow = static_cast<std::size_t>(region.extent.width) *
                          transfer.bytesPerPixel;
    const auto rowBytes = layout.bytesPerRow == 0 ? tightRow
                                                   : layout.bytesPerRow;
    if (rowBytes < tightRow ||
        rowBytes % transfer.bytesPerPixel != 0 ||
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
    if (imageHeightName != 0) {
        glPixelStorei(imageHeightName, static_cast<GLint>(rowsPerImage));
    }
}

inline void reset_pixel_store(GLenum alignmentName, GLenum rowLengthName,
                              GLenum imageHeightName) {
    if (imageHeightName != 0) {
        glPixelStorei(imageHeightName, 0);
    }
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
                                 "GL texture framebuffer is incomplete");
}

[[nodiscard]] inline Status write_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<const std::byte> data, const TextureDataLayout& layout) {
    const auto texture =
        std::static_pointer_cast<TextureResource>(nativeResource);
    if (!texture ||
        !texture_data_supported(*texture, region, layout, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL texture write region is invalid");
    }
    if (texture->desc.sampleCount != 1) {
        return Status::failure(
            StatusCode::unsupported,
            "GL multisample textures cannot be written directly");
    }
    std::lock_guard lock{texture->context->mutex};
    if (auto status = make_current(*texture->context); !status.ok()) {
        return status;
    }
    const auto rowBytes = layout.bytesPerRow == 0
                              ? static_cast<std::size_t>(region.extent.width) *
                                    transfer_format(
                                        texture->format,
                                        region.subresource.aspect)
                                        .bytesPerPixel
                              : layout.bytesPerRow;
    const auto format = transfer_format(texture->format,
                                        region.subresource.aspect);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(texture->target, texture->name);
    const auto rowsPerImage = layout.rowsPerImage == 0
                                  ? region.extent.height
                                  : layout.rowsPerImage;
    configure_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                          GL_UNPACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                          format.bytesPerPixel);
    const auto* source = data.data() + layout.offset;
    if (texture->desc.dimension == TextureDimension::cube &&
        texture->desc.arrayLayers == 6) {
        glTexSubImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.subresource.arrayLayer,
            static_cast<GLint>(region.subresource.mipLevel),
            static_cast<GLint>(region.origin.x),
            static_cast<GLint>(region.origin.y),
            static_cast<GLsizei>(region.extent.width),
            static_cast<GLsizei>(region.extent.height), format.format,
            format.type, source);
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
            static_cast<GLsizei>(depth), format.format, format.type, source);
    } else {
        glTexSubImage2D(texture->target,
                        static_cast<GLint>(region.subresource.mipLevel),
                        static_cast<GLint>(region.origin.x),
                        static_cast<GLint>(region.origin.y),
                        static_cast<GLsizei>(region.extent.width),
                        static_cast<GLsizei>(region.extent.height),
                        format.format, format.type, source);
    }
    reset_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                      GL_UNPACK_IMAGE_HEIGHT);
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "GL texture write reported an error");
}

[[nodiscard]] inline Status read_texture(
    const std::shared_ptr<void>& nativeResource, const TextureRegion& region,
    std::span<std::byte> data, const TextureDataLayout& layout) {
    const auto texture =
        std::static_pointer_cast<TextureResource>(nativeResource);
    if (!texture ||
        !texture_data_supported(*texture, region, layout, data.size())) {
        return Status::failure(StatusCode::invalid_argument,
                               "GL texture read region is invalid");
    }
    if (region.extent.depth != 1 || texture->desc.sampleCount != 1) {
        return Status::failure(
            StatusCode::unsupported,
            "GL host readback supports one single-sample slice at a time");
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
                                    transfer_format(
                                        texture->format,
                                        region.subresource.aspect)
                                        .bytesPerPixel
                              : layout.bytesPerRow;
    const auto format = transfer_format(texture->format,
                                        region.subresource.aspect);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    const auto rowsPerImage = layout.rowsPerImage == 0
                                  ? region.extent.height
                                  : layout.rowsPerImage;
    configure_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                          packImageHeight, rowBytes, rowsPerImage,
                          format.bytesPerPixel);
    glReadPixels(static_cast<GLint>(region.origin.x),
                 static_cast<GLint>(region.origin.y),
                 static_cast<GLsizei>(region.extent.width),
                 static_cast<GLsizei>(region.extent.height),
                 format.format, format.type,
                 data.data() + layout.offset);
    reset_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                      packImageHeight);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glFinish();
    return glGetError() == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "GL texture read reported an error");
}

struct Probe {
    std::shared_ptr<Context> context;
    std::string adapterName;
    bool debugOutput = false;
    bool textureViews = false;
    std::uint32_t uniformAlignment = 1;
    std::uint32_t storageAlignment = 1;
    Extent3D workgroupSize{0, 0, 0};
    std::uint32_t workgroupInvocations = 0;
};

[[nodiscard]] inline Status framebuffer_smoke() {
    // Offscreen contexts may have no default framebuffer, so the clear and
    // readback smoke always targets a temporary renderbuffer.
    GLuint framebuffer = 0;
    GLuint renderbuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1, 1);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffer);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    std::array<unsigned char, 4> pixel{};
    if (complete) {
        glViewport(0, 0, 1, 1);
        glClearColor(0.25F, 0.5F, 0.75F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    }
    const auto error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteFramebuffers(1, &framebuffer);
    if (!complete || error != GL_NO_ERROR || pixel[0] < 60 || pixel[0] > 68 ||
        pixel[1] < 124 || pixel[1] > 132 || pixel[2] < 188 ||
        pixel[2] > 196 || pixel[3] != 255) {
        return Status::failure(
            StatusCode::backend_validation_failed,
            "the native GL clear/readback smoke result was not deterministic");
    }
    return Status::success();
}

[[nodiscard]] inline Result<Probe> initialize(const InstanceDesc& desc) {
    auto native = std::make_shared<Context>();
    if (auto status = create_context(*native); !status.ok()) {
        return status;
    }

    native->debugCallback = desc.debugCallback;
    native->debugUserData = desc.debugUserData;
#if defined(TRUFFLE_GL_HAS_DEBUG_OUTPUT)
    if (desc.enableValidation) {
#if defined(TRUFFLE_GL_API_DESKTOP)
        constexpr auto callbackName = "glDebugMessageCallback";
#else
        constexpr auto callbackName = "glDebugMessageCallbackKHR";
#endif
        const auto debugCallback = reinterpret_cast<DebugMessageCallbackProc>(
            context_proc_address(callbackName));
        if (debugCallback != nullptr) {
            glEnable(debugOutput);
            glEnable(debugOutputSynchronous);
            debugCallback(&debug_message, native.get());
            native->debugOutput = glGetError() == GL_NO_ERROR;
        }
    }
#endif

    if (auto status = framebuffer_smoke(); !status.ok()) {
        return status;
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::string name = renderer != nullptr ? renderer : "GL adapter";
    if (version != nullptr) {
        name += " (";
        name += version;
        name += ')';
    }
    if (native->debugOutput) {
        name += " [KHR_debug]";
    }
#if defined(TRUFFLE_GL_HAS_TEXTURE_VIEWS)
    GLint majorVersion = 0;
    GLint minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    const bool textureViews = majorVersion > 4 ||
                              (majorVersion == 4 && minorVersion >= 3);
#else
    // Without glTextureView, views are restricted to full identity views.
    constexpr bool textureViews = true;
#endif
    GLint uniformAlignment = 1;
    GLint storageAlignment = 1;
    GLint invocations = 0;
    std::array<GLint, 3> workgroup{};
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformAlignment);
#if defined(TRUFFLE_GL_HAS_COMPUTE)
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storageAlignment);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);
    for (GLuint index = 0; index < 3; ++index) {
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, index, &workgroup[index]);
    }
#endif
    const bool validationDebugOutput =
        desc.enableValidation && native->debugOutput;
    return Probe{.context = std::move(native),
                 .adapterName = std::move(name),
                 .debugOutput = validationDebugOutput,
                 .textureViews = textureViews,
                 .uniformAlignment = static_cast<std::uint32_t>(uniformAlignment),
                 .storageAlignment = static_cast<std::uint32_t>(storageAlignment),
                 .workgroupSize = {static_cast<std::uint32_t>(workgroup[0]),
                                    static_cast<std::uint32_t>(workgroup[1]),
                                    static_cast<std::uint32_t>(workgroup[2])},
                 .workgroupInvocations = static_cast<std::uint32_t>(invocations)};
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
                                   "GL buffer-to-texture resources are invalid");
        }
        if (destinationTexture->desc.sampleCount != 1) {
            return Status::failure(
                StatusCode::unsupported,
                "GL multisample textures cannot be uploaded directly");
        }
        const auto& region = transfer.bufferTexture.texture;
        const auto& layout = transfer.bufferTexture.layout;
        const auto format = transfer_format(
            destinationTexture->format, region.subresource.aspect);
        const auto rowBytes = layout.bytesPerRow == 0
                                  ? static_cast<std::size_t>(region.extent.width) *
                                        format.bytesPerPixel
                                  : layout.bytesPerRow;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sourceBuffer->name);
        glBindTexture(destinationTexture->target, destinationTexture->name);
        const auto rowsPerImage = layout.rowsPerImage == 0
                                      ? region.extent.height
                                      : layout.rowsPerImage;
        configure_pixel_store(GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
                              GL_UNPACK_IMAGE_HEIGHT, rowBytes, rowsPerImage,
                              format.bytesPerPixel);
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
                format.format, format.type, source);
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
                format.format, format.type, source);
        } else {
            glTexSubImage2D(
                destinationTexture->target,
                static_cast<GLint>(region.subresource.mipLevel),
                static_cast<GLint>(region.origin.x),
                static_cast<GLint>(region.origin.y),
                static_cast<GLsizei>(region.extent.width),
                static_cast<GLsizei>(region.extent.height),
                format.format, format.type, source);
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
                                   "GL texture-to-buffer resources are invalid");
        }
        if (sourceTexture->desc.sampleCount != 1) {
            return Status::failure(
                StatusCode::unsupported,
                "GL multisample textures require resolve before readback");
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
        const auto format = transfer_format(sourceTexture->format,
                                            region.subresource.aspect);
        const auto rowBytes = layout.bytesPerRow == 0
                                  ? static_cast<std::size_t>(region.extent.width) *
                                        format.bytesPerPixel
                                  : layout.bytesPerRow;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, destinationBuffer->name);
        const auto rowsPerImage = layout.rowsPerImage == 0
                                      ? region.extent.height
                                      : layout.rowsPerImage;
        configure_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                              packImageHeight, rowBytes, rowsPerImage,
                              format.bytesPerPixel);
        const auto offset = transfer.bufferTexture.bufferOffset + layout.offset;
        glReadPixels(static_cast<GLint>(region.origin.x),
                     static_cast<GLint>(region.origin.y),
                     static_cast<GLsizei>(region.extent.width),
                     static_cast<GLsizei>(region.extent.height),
                     format.format, format.type,
                     reinterpret_cast<void*>(offset));
        reset_pixel_store(GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                          packImageHeight);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
    } else if (transfer.kind == NativeTransferKind::clear_texture) {
        if (!destinationTexture ||
            !texture_region_supported(*destinationTexture,
                                      transfer.texture.destination) ||
            transfer.texture.destination.extent.depth != 1) {
            return Status::failure(StatusCode::invalid_argument,
                                   "GL texture clear resource is invalid");
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
                                   "GL texture copy resources are invalid");
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
                                   "GL texture copy regions are invalid");
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
                               "this GL texture transfer is unsupported");
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
    // Semaphore waits were reached on the host before this call, and this
    // submission completes before the foundation publishes its signals.
    (void)waits;
    (void)signals;
    const auto context = std::static_pointer_cast<Context>(nativeContext);
    if (!context || !context_available(*context)) {
        return Status::failure(StatusCode::device_lost,
                               "the native GL context is unavailable");
    }
    std::lock_guard lock{context->mutex};
    if (auto status = make_current(*context); !status.ok()) {
        return status;
    }
    std::vector<GLuint> transients;
    GLuint renderFramebuffer = 0;
    GLuint vertexArray = 0;
    bool renderActive = false;
    std::shared_ptr<PipelineResource> graphicsPipeline;
    std::shared_ptr<PipelineResource> computePipeline;
    bool computeActive = false;
    std::vector<NativeBindingResource> boundResources;
    std::array<GLuint, 3> pushBuffers{};
    graphics::InputState input;
    Extent2D renderExtent;
    std::vector<NativeRenderAttachment> renderAttachments;
    const auto cleanup = [&] {
        glUseProgram(0);
        if (renderFramebuffer != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &renderFramebuffer);
            renderFramebuffer = 0;
        }
        if (vertexArray != 0) {
            glBindVertexArray(0);
            glDeleteVertexArrays(1, &vertexArray);
            vertexArray = 0;
        }
        if (!transients.empty()) {
            glDeleteBuffers(static_cast<GLsizei>(transients.size()),
                            transients.data());
        }
    };
    const auto fail = [&](Status status) {
        cleanup();
        return status;
    };
    const auto bind_pushes = [&](const program::Layout& layout) {
        for (const auto& push : layout.pushes) {
            auto& buffer = pushBuffers[static_cast<std::size_t>(push.stage)];
            if (buffer == 0) {
                glGenBuffers(1, &buffer);
                transients.push_back(buffer);
                glBindBuffer(GL_UNIFORM_BUFFER, buffer);
                const std::array<std::byte, 128> zero{};
                glBufferData(GL_UNIFORM_BUFFER, 128, zero.data(), GL_STREAM_DRAW);
            }
            glBindBufferBase(GL_UNIFORM_BUFFER, push.slot, buffer);
        }
    };
    for (const auto& command : commands) {
        if (command.kind == NativeCommandKind::barrier) {
#if defined(TRUFFLE_GL_HAS_COMPUTE)
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
#endif
            continue;
        }
        if (command.kind == NativeCommandKind::write_timestamp ||
            command.kind == NativeCommandKind::begin_occlusion_query ||
            command.kind == NativeCommandKind::end_occlusion_query ||
            command.kind == NativeCommandKind::resolve_queries) {
#if defined(TRUFFLE_GL_HAS_SYNC_OBJECTS)
            const auto destination =
                std::static_pointer_cast<BufferResource>(command.secondaryObject);
            if (auto status = execute_query(
                    command, destination ? destination->name : 0);
                !status.ok()) {
                return fail(status);
            }
            continue;
#else
            return fail(Status::failure(StatusCode::unsupported,
                                        "GL queries are not enabled here"));
#endif
        }
        if (command.kind == NativeCommandKind::begin_compute) {
#if defined(TRUFFLE_GL_HAS_COMPUTE)
            if (renderActive || computeActive) {
                return fail(Status::failure(StatusCode::invalid_state,
                                             "GL compute scope overlaps another encoder"));
            }
            computeActive = true;
            computePipeline.reset();
            boundResources.clear();
            continue;
#else
            return fail(Status::failure(StatusCode::unsupported,
                                         "GL compute is unsupported by this context"));
#endif
        }
        if (command.kind == NativeCommandKind::end_compute) {
            if (!computeActive) {
                return fail(Status::failure(StatusCode::invalid_state,
                                             "GL compute scope is not active"));
            }
            computeActive = false;
            computePipeline.reset();
            glUseProgram(0);
            continue;
        }
        if (command.kind == NativeCommandKind::bind_compute_pipeline) {
            computePipeline = std::static_pointer_cast<PipelineResource>(command.object);
            if (!computeActive || !computePipeline || !computePipeline->isCompute) {
                return fail(Status::failure(StatusCode::invalid_state,
                                             "GL compute pipeline is invalid"));
            }
            glUseProgram(computePipeline->program);
            bind_pushes(computePipeline->bindings);
            continue;
        }
        if (command.kind == NativeCommandKind::bind_group) {
            const auto group = command.arguments[0];
            std::erase_if(boundResources, [&](const NativeBindingResource& resource) {
                return resource.group == group;
            });
            boundResources.insert(boundResources.end(), command.bindings.begin(),
                                   command.bindings.end());
            continue;
        }
        if (command.kind == NativeCommandKind::push_constants) {
            const auto pipeline = renderActive ? graphicsPipeline : computePipeline;
            if (!pipeline) {
                return fail(Status::failure(StatusCode::invalid_state,
                                             "GL push constants require a pipeline"));
            }
            bind_pushes(pipeline->bindings);
            for (const auto& push : pipeline->bindings.pushes) {
                const auto mask = push.stage == ShaderStage::vertex ? ShaderStageMask::vertex :
                                  push.stage == ShaderStage::fragment ? ShaderStageMask::fragment :
                                                                         ShaderStageMask::compute;
                if (!has_stage(static_cast<ShaderStageMask>(command.arguments[0]), mask)) continue;
                glBindBuffer(GL_UNIFORM_BUFFER,
                              pushBuffers[static_cast<std::size_t>(push.stage)]);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                 static_cast<GLintptr>(command.arguments[1]),
                                 static_cast<GLsizeiptr>(command.bytes.size()),
                                 command.bytes.data());
            }
            continue;
        }
        if (command.kind == NativeCommandKind::bind_uniform_buffer ||
            command.kind == NativeCommandKind::bind_storage_buffer) {
            const auto type = command.kind == NativeCommandKind::bind_uniform_buffer
                                  ? BindingType::uniform_buffer : BindingType::storage_buffer;
            std::erase_if(boundResources, [&](const NativeBindingResource& resource) {
                return resource.group == 0 && resource.binding == command.arguments[0];
            });
            boundResources.push_back({
                .binding = static_cast<std::uint32_t>(command.arguments[0]), .type = type,
                .resource = command.object,
                .offset = static_cast<std::size_t>(command.arguments[1]), .size = whole_size});
            continue;
        }
        if (command.kind == NativeCommandKind::dispatch ||
            command.kind == NativeCommandKind::dispatch_indirect) {
#if defined(TRUFFLE_GL_HAS_COMPUTE)
            if (!computeActive || !computePipeline) {
                return fail(Status::failure(StatusCode::invalid_state,
                                             "GL dispatch requires a compute pipeline"));
            }
            if (auto status = bind_resources(computePipeline->bindings, boundResources);
                !status.ok()) return fail(status);
            if (command.kind == NativeCommandKind::dispatch_indirect) {
                const auto buffer = std::static_pointer_cast<BufferResource>(command.object);
                glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, buffer->name);
                glDispatchComputeIndirect(static_cast<GLintptr>(command.arguments[0]));
                glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
            } else {
                for (GLuint index = 0; index < 3; ++index) {
                    GLint limit = 0;
                    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, index, &limit);
                    if (command.arguments[index] > static_cast<std::uint64_t>(limit)) {
                        return fail(Status::failure(StatusCode::invalid_argument,
                                                     "GL dispatch exceeds native group limits"));
                    }
                }
                glDispatchCompute(static_cast<GLuint>(command.arguments[0]),
                                   static_cast<GLuint>(command.arguments[1]),
                                   static_cast<GLuint>(command.arguments[2]));
            }
            continue;
#else
            return fail(Status::failure(StatusCode::unsupported,
                                         "GL compute is unsupported by this context"));
#endif
        }
        if (command.kind == NativeCommandKind::begin_render) {
            if (renderActive || computeActive) {
                return fail(Status::failure(
                    StatusCode::invalid_state, "GL rendering is already active"));
            }
            glGenFramebuffers(1, &renderFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, renderFramebuffer);
            std::vector<GLenum> drawBuffers;
            drawBuffers.reserve(command.colorAttachments.size());
            for (std::size_t index = 0;
                 index < command.colorAttachments.size(); ++index) {
                const auto texture = std::static_pointer_cast<TextureResource>(
                    command.colorAttachments[index].texture);
                if (!texture || texture->desc.dimension != TextureDimension::d2 ||
                    texture->desc.arrayLayers != 1) {
                    return fail(Status::failure(
                        StatusCode::unsupported,
                        "GL baseline render attachments must be 2D"));
                }
                const auto attachment =
                    GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index);
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                                       texture->target, texture->name, 0);
                drawBuffers.push_back(attachment);
            }
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()),
                          drawBuffers.data());
            const auto depth = std::static_pointer_cast<TextureResource>(
                command.depthStencilAttachment.texture);
            if (depth) {
                const auto attachment =
                    has_aspect(depth->format.aspects, TextureAspect::stencil)
                        ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
                glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, depth->target,
                                       depth->name, 0);
            }
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE) {
                return fail(Status::failure(
                    StatusCode::backend_validation_failed,
                    "GL render framebuffer is incomplete"));
            }
            glViewport(0, 0, static_cast<GLsizei>(command.extent.width),
                       static_cast<GLsizei>(command.extent.height));
            glDisable(GL_SCISSOR_TEST);
            glDepthMask(GL_TRUE);
            glStencilMask(0xffffffffu);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            for (std::size_t index = 0;
                 index < command.colorAttachments.size(); ++index) {
#if defined(TRUFFLE_GL_API_DESKTOP)
                glColorMaski(static_cast<GLuint>(index), GL_TRUE, GL_TRUE,
                             GL_TRUE, GL_TRUE);
#endif
                const auto& attachment = command.colorAttachments[index];
                if (attachment.loadOp == LoadOp::clear) {
                    const std::array clear{attachment.clear.r,
                                           attachment.clear.g,
                                           attachment.clear.b,
                                           attachment.clear.a};
                    glClearBufferfv(GL_COLOR, static_cast<GLint>(index),
                                    clear.data());
                }
            }
            if (depth) {
                const auto& attachment = command.depthStencilAttachment;
                if (attachment.depthLoadOp == LoadOp::clear) {
                    glClearBufferfv(GL_DEPTH, 0, &attachment.clearDepth);
                }
                if (has_aspect(depth->format.aspects, TextureAspect::stencil) &&
                    attachment.stencilLoadOp == LoadOp::clear) {
                    const auto stencil = static_cast<GLint>(attachment.clearStencil);
                    glClearBufferiv(GL_STENCIL, 0, &stencil);
                }
            }
            glGenVertexArrays(1, &vertexArray);
            glBindVertexArray(vertexArray);
            renderActive = true;
            graphicsPipeline.reset();
            boundResources.clear();
            input = {};
            renderExtent = command.extent;
            renderAttachments = command.colorAttachments;
            continue;
        }
        if (command.kind == NativeCommandKind::end_render) {
            if (!renderActive) {
                return fail(Status::failure(
                    StatusCode::invalid_state,
                    "GL render encoder is not active"));
            }
            for (std::size_t index = 0; index < renderAttachments.size(); ++index) {
                const auto resolve = std::static_pointer_cast<TextureResource>(
                    renderAttachments[index].resolveTexture);
                if (!resolve) continue;
                GLuint framebuffer = 0;
                glGenFramebuffers(1, &framebuffer);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       resolve->target, resolve->name, 0);
                constexpr GLenum color = GL_COLOR_ATTACHMENT0;
                glDrawBuffers(1, &color);
                if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
                    GL_FRAMEBUFFER_COMPLETE) {
                    glDeleteFramebuffers(1, &framebuffer);
                    return fail(Status::failure(
                        StatusCode::backend_validation_failed,
                        "GL resolve framebuffer is incomplete"));
                }
                glBindFramebuffer(GL_READ_FRAMEBUFFER, renderFramebuffer);
                glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
                glDisable(GL_SCISSOR_TEST);
                glBlitFramebuffer(0, 0, static_cast<GLint>(renderExtent.width),
                                  static_cast<GLint>(renderExtent.height), 0, 0,
                                  static_cast<GLint>(renderExtent.width),
                                  static_cast<GLint>(renderExtent.height),
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glDeleteFramebuffers(1, &framebuffer);
            }
            glUseProgram(0);
            glBindVertexArray(0);
            glDeleteVertexArrays(1, &vertexArray);
            vertexArray = 0;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &renderFramebuffer);
            renderFramebuffer = 0;
            renderActive = false;
            graphicsPipeline.reset();
            continue;
        }
        if (command.kind == NativeCommandKind::bind_graphics_pipeline) {
            graphicsPipeline =
                std::static_pointer_cast<PipelineResource>(command.object);
            if (!renderActive || !graphicsPipeline ||
                graphicsPipeline->isCompute || graphicsPipeline->program == 0) {
                return fail(Status::failure(
                    StatusCode::invalid_argument,
                    "GL graphics pipeline binding is invalid"));
            }
            glUseProgram(graphicsPipeline->program);
            graphics::apply(graphicsPipeline->graphics, renderExtent);
            bind_pushes(graphicsPipeline->bindings);
            continue;
        }
        if (command.kind == NativeCommandKind::bind_vertex_buffer) {
            const auto buffer = std::static_pointer_cast<BufferResource>(command.object);
            const auto slot = command.arguments[0];
            if (!renderActive || !buffer || slot >= input.vertices.size()) {
                return fail(Status::failure(StatusCode::invalid_argument,
                                             "GL vertex binding is invalid"));
            }
            input.vertices[slot] = {buffer->name,
                                    static_cast<std::size_t>(command.arguments[1]),
                                    buffer->size};
            continue;
        }
        if (command.kind == NativeCommandKind::bind_index_buffer) {
            const auto buffer = std::static_pointer_cast<BufferResource>(command.object);
            if (!renderActive || !buffer) {
                return fail(Status::failure(StatusCode::invalid_argument,
                                             "GL index binding is invalid"));
            }
            input.indexBuffer = buffer->name;
            input.indexOffset = static_cast<std::size_t>(command.arguments[0]);
            input.indexSize = buffer->size;
            input.indexFormat = static_cast<IndexFormat>(command.arguments[1]);
            continue;
        }
        if (command.kind == NativeCommandKind::set_viewports) {
            graphics::viewport(command.viewports.front());
            continue;
        }
        if (command.kind == NativeCommandKind::set_scissors) {
            graphics::scissor(command.scissors.front());
            continue;
        }
        if (command.kind == NativeCommandKind::set_blend_constant) {
            std::array<float, 4> color{};
            std::memcpy(color.data(), command.bytes.data(), sizeof(color));
            glBlendColor(color[0], color[1], color[2], color[3]);
            continue;
        }
        if (command.kind == NativeCommandKind::set_stencil_reference) {
            graphics::stencil_reference(graphicsPipeline->graphics.depthStencil,
                                         static_cast<std::uint32_t>(command.arguments[0]));
            continue;
        }
        if (command.kind == NativeCommandKind::set_depth_bias) {
            std::array<float, 3> bias{};
            std::memcpy(bias.data(), command.bytes.data(), sizeof(bias));
            if (bias[2] != 0.0F) {
                return fail(Status::failure(StatusCode::unsupported,
                                             "GL clamped depth bias is unsupported"));
            }
            graphics::depth_bias(bias[0], bias[1]);
            continue;
        }
        if (command.kind == NativeCommandKind::draw ||
            command.kind == NativeCommandKind::draw_indexed ||
            command.kind == NativeCommandKind::draw_indirect) {
            if (!renderActive || !graphicsPipeline) {
                return fail(Status::failure(
                    StatusCode::invalid_state,
                    "GL draw requires an active graphics pipeline"));
            }
            if (auto status = input.apply(graphicsPipeline->graphics); !status.ok()) {
                return fail(status);
            }
            if (auto status = bind_resources(graphicsPipeline->bindings, boundResources);
                !status.ok()) return fail(status);
            const auto mode = graphics::topology(graphicsPipeline->graphics.topology);
            const auto indexType = input.indexFormat == IndexFormat::uint16
                                       ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
            if (command.kind == NativeCommandKind::draw_indirect) {
#if defined(TRUFFLE_GL_API_DESKTOP)
                const auto buffer = std::static_pointer_cast<BufferResource>(command.object);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer->name);
                for (std::uint64_t index = 0; index < command.arguments[2]; ++index) {
                    const auto offset = command.arguments[0] + index * command.arguments[3];
#if !defined(TRUFFLE_GL_HAS_BASE_INSTANCE)
                    // GL 4.1 reserves the indirect first-instance word and
                    // requires it to be zero; reject records that set it.
                    GLuint firstInstance = 0;
                    glGetBufferSubData(
                        GL_DRAW_INDIRECT_BUFFER,
                        static_cast<GLintptr>(offset + (command.arguments[1] != 0 ? 16u : 12u)),
                        sizeof(firstInstance), &firstInstance);
                    if (firstInstance != 0) {
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                        return fail(Status::failure(
                            StatusCode::unsupported,
                            "GL indirect first instance requires a GL 4.2 or newer context"));
                    }
#endif
                    if (command.arguments[1] != 0) {
                        if (input.indexOffset != 0) {
                            return fail(Status::failure(
                                StatusCode::unsupported,
                                "GL indexed indirect draws require a zero index-buffer offset"));
                        }
                        glDrawElementsIndirect(mode, indexType,
                                                reinterpret_cast<const void*>(offset));
                    } else {
                        glDrawArraysIndirect(mode, reinterpret_cast<const void*>(offset));
                    }
                }
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#else
                return fail(Status::failure(StatusCode::unsupported,
                                             "GLES indirect drawing is unsupported"));
#endif
                continue;
            }
            const auto count = static_cast<GLsizei>(command.arguments[0]);
            const auto instances = static_cast<GLsizei>(command.arguments[1]);
            if (command.kind == NativeCommandKind::draw_indexed) {
                const auto indexBytes = input.indexFormat == IndexFormat::uint16 ? 2u : 4u;
                const auto first = command.arguments[2];
                if (input.indexOffset > input.indexSize ||
                    first > (input.indexSize - input.indexOffset) / indexBytes ||
                    command.arguments[0] >
                        (input.indexSize - input.indexOffset) / indexBytes - first) {
                    return fail(Status::failure(StatusCode::invalid_argument,
                                                 "GL indexed draw exceeds its buffer"));
                }
                const auto* offset = reinterpret_cast<const void*>(
                    input.indexOffset + first * indexBytes);
                const auto baseVertex = static_cast<std::int32_t>(command.arguments[3]);
#if defined(TRUFFLE_GL_HAS_BASE_INSTANCE)
                glDrawElementsInstancedBaseVertexBaseInstance(
                    mode, count, indexType, offset, instances, baseVertex,
                    static_cast<GLuint>(command.arguments[4]));
#elif defined(TRUFFLE_GL_API_DESKTOP)
                if (command.arguments[4] != 0) {
                    return fail(Status::failure(
                        StatusCode::unsupported,
                        "GL base instance requires a GL 4.2 or newer context"));
                }
                glDrawElementsInstancedBaseVertex(mode, count, indexType, offset,
                                                  instances, baseVertex);
#else
                if (baseVertex != 0 || command.arguments[4] != 0) {
                    return fail(Status::failure(
                        StatusCode::unsupported,
                        "GLES base vertex and base instance are unsupported"));
                }
                glDrawElementsInstanced(mode, count, indexType, offset, instances);
#endif
            } else {
#if defined(TRUFFLE_GL_HAS_BASE_INSTANCE)
                glDrawArraysInstancedBaseInstance(
                    mode, static_cast<GLint>(command.arguments[2]), count, instances,
                    static_cast<GLuint>(command.arguments[3]));
#else
                if (command.arguments[3] != 0) {
                    return fail(Status::failure(
                        StatusCode::unsupported,
#if defined(TRUFFLE_GL_API_ES)
                        "GLES base instance is unsupported"));
#else
                        "GL base instance requires a GL 4.2 or newer context"));
#endif
                }
                glDrawArraysInstanced(mode, static_cast<GLint>(command.arguments[2]),
                                      count, instances);
#endif
            }
            continue;
        }
        if (command.kind != NativeCommandKind::transfer) {
            return fail(Status::failure(
                StatusCode::unsupported,
                "this GL pipeline command is not implemented"));
        }
        if (renderActive) {
            return fail(Status::failure(
                StatusCode::invalid_state,
                "GL transfers cannot overlap a render encoder"));
        }
        const auto& transfer = command.transfer;
        if (transfer.kind == NativeTransferKind::copy_buffer) {
            const auto source =
                std::static_pointer_cast<BufferResource>(transfer.source);
            const auto destination =
                std::static_pointer_cast<BufferResource>(transfer.destination);
            if (!source || !destination) {
                return Status::failure(StatusCode::invalid_argument,
                                       "GL buffer copy resources are invalid");
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
                return fail(status);
            }
            continue;
        }
        const auto destination =
            std::static_pointer_cast<BufferResource>(transfer.destination);
        if (!destination) {
            return Status::failure(StatusCode::invalid_argument,
                                   "GL buffer fill resource is invalid");
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
    if (renderActive) {
        return fail(Status::failure(StatusCode::invalid_state,
                                    "GL render encoder was not ended"));
    }
    if (auto status = wait_for_completion("GL submission"); !status.ok()) {
        return fail(status);
    }
    const auto error = glGetError();
    cleanup();
    return error == GL_NO_ERROR
               ? Status::success()
               : Status::failure(StatusCode::backend_validation_failed,
                                 "the native GL queue reported an error");
}

} // namespace truffle::rhi::detail::gl_probe

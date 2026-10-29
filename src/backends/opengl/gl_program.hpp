#pragma once

#include "foundation_backend.hpp"
#include "gl_api.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace truffle::rhi::detail::gl_probe::program {

struct Binding {
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::uint32_t element = 0;
    BindingType type = BindingType::uniform_buffer;
    GLuint slot = 0;
    std::size_t minimumSize = 0;
    GLenum uniformType = 0;
    GLuint nativeIdentity = GL_INVALID_INDEX;
};

struct PushBinding {
    ShaderStage stage = ShaderStage::vertex;
    GLuint slot = 0;
};

struct Layout {
    std::vector<Binding> bindings;
    std::vector<PushBinding> pushes;
};

inline Status mismatch(std::string message) {
    return Status::failure(StatusCode::invalid_argument, std::move(message));
}

inline Result<Layout> configure(GLuint name,
                                std::span<const ShaderDesc* const> shaders,
                                const NativePipelineLayout& layout,
                                std::span<const SpecializationValue> values) {
    Layout result;
    std::vector<GLuint> uniformBlocks;
    std::vector<GLuint> storageBlocks;
    std::vector<GLint> uniformLocations;
    GLint uniformLimit = 0;
    GLint textureLimit = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &uniformLimit);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureLimit);
#if defined(TRUFFLE_GL_HAS_COMPUTE)
    GLint storageLimit = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &storageLimit);
#endif
    const auto entry_for = [&](const ResourceBinding& resource)
        -> const BindGroupLayoutEntry* {
        for (const auto& group : layout.bindGroups) {
            if (group.group != resource.group) continue;
            for (const auto& entry : group.entries) {
                if (entry.binding == resource.binding) return &entry;
            }
        }
        return nullptr;
    };
    const auto default_slot = [&](const ResourceBinding& resource,
                                  BindingType type, std::uint32_t element) {
        std::uint32_t slot = 0;
        for (const auto& group : layout.bindGroups) {
            for (const auto& entry : group.entries) {
                if (group.group == resource.group && entry.binding == resource.binding) {
                    for (const auto& candidate : group.entries) {
                        if (candidate.type == type && candidate.binding < entry.binding) {
                            slot += candidate.arrayCount;
                        }
                    }
                    return slot + element;
                }
            }
            for (const auto& entry : group.entries) {
                if (entry.type == type) slot += entry.arrayCount;
            }
        }
        return slot + element;
    };
    const auto mapped_slot = [&](const ShaderDesc& shader,
                                 const ResourceBinding& resource,
                                 std::uint32_t element, GLuint fallback) -> Result<GLuint> {
        for (const auto& mapping : shader.bindingMap) {
            if (mapping.group == resource.group && mapping.binding == resource.binding &&
                mapping.arrayElement == element) {
                if (mapping.nativeGroup != 0 ||
                    mapping.nativeBinding > std::numeric_limits<GLuint>::max() -
                                                mapping.nativeArrayElement) {
                    return mismatch("GL binding remap exceeds the flat native namespace");
                }
                return mapping.nativeBinding + mapping.nativeArrayElement;
            }
        }
        return fallback;
    };
    glUseProgram(name);
    const auto configure_resources = [&](bool samplers) -> Status {
        for (const auto* shader : shaders) {
            for (const auto& resource : shader->reflection) {
                const auto* entry = entry_for(resource);
                if (!entry || resource.name.empty()) {
                    return mismatch("GL reflection requires named, layout-owned resources");
                }
                if ((entry->type == BindingType::sampler) != samplers) continue;
                for (std::uint32_t element = 0; element < resource.arrayCount; ++element) {
                    const auto resourceName = resource.name +
                        (resource.arrayCount > 1 ? "[" + std::to_string(element) + "]" : "");
                    Binding binding{resource.group, resource.binding, element, entry->type};
                    auto slot = mapped_slot(*shader, resource, element,
                                             default_slot(resource, entry->type, element));
                    if (!slot.ok()) return slot.status();
                    binding.slot = slot.value();
                    if (entry->type == BindingType::uniform_buffer) {
                        const auto index = glGetUniformBlockIndex(name, resourceName.c_str());
                        if (index == GL_INVALID_INDEX ||
                            binding.slot >= static_cast<GLuint>(uniformLimit - 3)) {
                            return mismatch("GL uniform block or binding limit disagrees with reflection");
                        }
                        GLint size = 0;
                        glGetActiveUniformBlockiv(name, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
                        if (size < 0 || static_cast<std::size_t>(size) > resource.minimumSize ||
                            static_cast<std::size_t>(size) > entry->minimumBufferSize) {
                            return mismatch("GL uniform block size disagrees with reflection");
                        }
                        binding.minimumSize = static_cast<std::size_t>(size);
                        binding.nativeIdentity = index;
                        uniformBlocks.push_back(index);
                        glUniformBlockBinding(name, index, binding.slot);
                    } else if (entry->type == BindingType::storage_buffer) {
#if defined(TRUFFLE_GL_HAS_COMPUTE)
                        const auto index = glGetProgramResourceIndex(
                            name, GL_SHADER_STORAGE_BLOCK, resourceName.c_str());
                        if (index == GL_INVALID_INDEX ||
                            binding.slot >= static_cast<GLuint>(storageLimit)) {
                            return mismatch("GL storage block or binding limit disagrees with reflection");
                        }
                        constexpr GLenum property = GL_BUFFER_DATA_SIZE;
                        GLint size = 0;
                        glGetProgramResourceiv(name, GL_SHADER_STORAGE_BLOCK, index, 1,
                                               &property, 1, nullptr, &size);
                        if (size < 0 || static_cast<std::size_t>(size) > resource.minimumSize) {
                            return mismatch("GL storage block size disagrees with reflection");
                        }
                        binding.minimumSize = static_cast<std::size_t>(size);
                        binding.nativeIdentity = index;
                        storageBlocks.push_back(index);
                        glShaderStorageBlockBinding(name, index, binding.slot);
#else
                        return Status::failure(StatusCode::unsupported,
                                               "GL storage blocks are unsupported by this context");
#endif
                    } else if (entry->type == BindingType::sampled_texture ||
                               entry->type == BindingType::sampler) {
                        const auto location = glGetUniformLocation(name, resourceName.c_str());
                        auto activeName = resourceName;
                        const auto bracket = activeName.rfind('[');
                        if (bracket != std::string::npos && activeName.back() == ']') {
                            activeName = activeName.substr(0, bracket) + "[0]";
                        }
                        const GLchar* uniformName = activeName.c_str();
                        GLuint index = GL_INVALID_INDEX;
                        glGetUniformIndices(name, 1, &uniformName, &index);
                        if (location < 0 || index == GL_INVALID_INDEX) {
                            return mismatch("GL sampler uniform disagrees with reflection");
                        }
                        GLint type = 0;
                        glGetActiveUniformsiv(name, 1, &index, GL_UNIFORM_TYPE, &type);
                        switch (type) {
                        case GL_SAMPLER_2D: case GL_SAMPLER_3D:
                        case GL_SAMPLER_CUBE: case GL_SAMPLER_2D_ARRAY:
                        case GL_SAMPLER_2D_SHADOW: case GL_SAMPLER_CUBE_SHADOW:
                        case GL_SAMPLER_2D_ARRAY_SHADOW:
                            break;
                        default:
                            return Status::failure(StatusCode::unsupported,
                                                   "GL sampled uniform type is unsupported");
                        }
                        binding.uniformType = static_cast<GLenum>(type);
                        binding.nativeIdentity = static_cast<GLuint>(location);
                        uniformLocations.push_back(location);
                        if (samplers) {
                            GLint paired = 0;
                            glGetUniformiv(name, location, &paired);
                            auto mapped = mapped_slot(*shader, resource, element,
                                                       static_cast<GLuint>(paired));
                            if (!mapped.ok()) return mapped.status();
                            if (mapped.value() != static_cast<GLuint>(paired)) {
                                return mismatch("GL separate sampler must map to its combined texture unit");
                            }
                            binding.slot = mapped.value();
                            const auto texture = std::find_if(
                                result.bindings.begin(), result.bindings.end(),
                                [&](const Binding& candidate) {
                                    return candidate.type == BindingType::sampled_texture &&
                                           candidate.slot == binding.slot;
                                });
                            if (texture == result.bindings.end()) {
                                return mismatch("GL sampler has no reflected paired texture");
                            }
                        }
                        if (binding.slot >= static_cast<GLuint>(textureLimit)) {
                            return mismatch("GL texture unit exceeds the native limit");
                        }
                        glUniform1i(location, static_cast<GLint>(binding.slot));
                    } else {
                        return Status::failure(StatusCode::unsupported,
                                               "GL storage images are not implemented");
                    }
                    for (const auto& previous : result.bindings) {
                        if (previous.type == binding.type &&
                            previous.nativeIdentity == binding.nativeIdentity &&
                            (previous.slot != binding.slot ||
                             previous.group != binding.group ||
                             previous.binding != binding.binding ||
                             previous.element != binding.element)) {
                            return mismatch("GL linked resources have conflicting logical identities");
                        }
                        if (previous.type == binding.type && previous.slot == binding.slot &&
                            (previous.group != binding.group ||
                             previous.binding != binding.binding ||
                             previous.element != binding.element)) {
                            return mismatch("GL logical resources collide at a native binding");
                        }
                    }
                    result.bindings.push_back(binding);
                }
            }
        }
        return Status::success();
    };
    if (auto status = configure_resources(false); !status.ok()) return status;
    if (auto status = configure_resources(true); !status.ok()) return status;
    for (const auto* shader : shaders) {
        if (!shader->pushConstants.empty()) {
            constexpr std::array names{"TruffleVertexPushConstants",
                                        "TruffleFragmentPushConstants",
                                        "TruffleComputePushConstants"};
            const auto stage = static_cast<std::size_t>(shader->stage);
            const auto index = glGetUniformBlockIndex(name, names[stage]);
            GLint size = 0;
            if (index == GL_INVALID_INDEX) {
                return mismatch("GL push constants require the stage-named uniform block");
            }
            glGetActiveUniformBlockiv(name, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
            std::uint64_t reflectedSize = 0;
            for (const auto& range : shader->pushConstants) {
                reflectedSize = std::max(reflectedSize,
                    static_cast<std::uint64_t>(range.offset) + range.size);
            }
            if (size <= 0 || size > 128 || static_cast<std::uint64_t>(size) > reflectedSize) {
                return mismatch("GL push block size disagrees with reflection");
            }
            const auto slot = static_cast<GLuint>(uniformLimit - 3 + static_cast<GLint>(stage));
            glUniformBlockBinding(name, index, slot);
            uniformBlocks.push_back(index);
            result.pushes.push_back({shader->stage, slot});
        }
        for (const auto& constant : shader->specializationConstants) {
            const auto location = glGetUniformLocation(name, constant.name.c_str());
            if (location < 0) {
                return mismatch("GL specialization requires an active named scalar uniform");
            }
            const GLchar* constantName = constant.name.c_str();
            GLuint index = GL_INVALID_INDEX;
            glGetUniformIndices(name, 1, &constantName, &index);
            GLint type = 0;
            GLint count = 0;
            if (index == GL_INVALID_INDEX) {
                return mismatch("GL specialization uniform is unavailable");
            }
            glGetActiveUniformsiv(name, 1, &index, GL_UNIFORM_TYPE, &type);
            glGetActiveUniformsiv(name, 1, &index, GL_UNIFORM_SIZE, &count);
            const auto expected = constant.type == ShaderValueType::boolean ? GL_BOOL :
                                  constant.type == ShaderValueType::sint32 ? GL_INT :
                                  constant.type == ShaderValueType::uint32 ? GL_UNSIGNED_INT :
                                                                           GL_FLOAT;
            if (type != expected || count != 1) {
                return mismatch("GL specialization uniform type disagrees with reflection");
            }
            uniformLocations.push_back(location);
            auto bits = constant.defaultValueBits;
            for (const auto& value : values) {
                if (value.id == constant.id) bits = value.valueBits;
            }
            switch (constant.type) {
            case ShaderValueType::uint32: glUniform1ui(location, bits); break;
            case ShaderValueType::sint32:
            case ShaderValueType::boolean:
                glUniform1i(location, static_cast<GLint>(bits)); break;
            case ShaderValueType::float32: {
                float value = 0;
                std::memcpy(&value, &bits, sizeof(value));
                glUniform1f(location, value);
                break;
            }
            }
        }
    }
    GLint blocks = 0;
    glGetProgramiv(name, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
    for (GLint index = 0; index < blocks; ++index) {
        if (std::find(uniformBlocks.begin(), uniformBlocks.end(),
                       static_cast<GLuint>(index)) == uniformBlocks.end()) {
            return mismatch("GL program contains an unreflected uniform block");
        }
    }
#if defined(TRUFFLE_GL_HAS_COMPUTE)
    glGetProgramInterfaceiv(name, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
    for (GLint index = 0; index < blocks; ++index) {
        if (std::find(storageBlocks.begin(), storageBlocks.end(),
                       static_cast<GLuint>(index)) == storageBlocks.end()) {
            return mismatch("GL program contains an unreflected storage block");
        }
    }
#endif
    GLint uniforms = 0;
    GLint maximumName = 0;
    glGetProgramiv(name, GL_ACTIVE_UNIFORMS, &uniforms);
    glGetProgramiv(name, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maximumName);
    std::vector<GLchar> uniformName(static_cast<std::size_t>(std::max(maximumName, 1)));
    for (GLint index = 0; index < uniforms; ++index) {
        GLint count = 0;
        GLenum type = 0;
        glGetActiveUniform(name, static_cast<GLuint>(index), maximumName, nullptr,
                            &count, &type, uniformName.data());
        const auto location = glGetUniformLocation(name, uniformName.data());
        if (location >= 0) {
            for (GLint element = 0; element < count; ++element) {
                if (std::find(uniformLocations.begin(), uniformLocations.end(),
                               location + element) == uniformLocations.end()) {
                    return mismatch("GL program contains an unreflected scalar or sampler uniform");
                }
            }
        }
    }
    glUseProgram(0);
    return result;
}

} // namespace truffle::rhi::detail::gl_probe::program

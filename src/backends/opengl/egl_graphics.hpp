#pragma once

#include "foundation_backend.hpp"

#include <array>
#include <cstring>

namespace truffle::rhi::detail::egl_probe::graphics {

inline GLenum compare(CompareOp value) {
    constexpr std::array<GLenum, 8> values{
        GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL,
        GL_GEQUAL, GL_ALWAYS};
    return values[static_cast<std::size_t>(value)];
}

inline GLenum stencil(StencilOp value) {
    constexpr std::array<GLenum, 8> values{
        GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT,
        GL_INCR_WRAP, GL_DECR_WRAP};
    return values[static_cast<std::size_t>(value)];
}

inline GLenum blend_factor(BlendFactor value) {
    constexpr std::array<GLenum, 10> values{
        GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
        GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    return values[static_cast<std::size_t>(value)];
}

inline GLenum blend_op(BlendOp value) {
    constexpr std::array<GLenum, 5> values{
        GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
    return values[static_cast<std::size_t>(value)];
}

inline GLenum topology(PrimitiveTopology value) {
    switch (value) {
    case PrimitiveTopology::triangle_list: return GL_TRIANGLES;
    case PrimitiveTopology::triangle_strip: return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::line_list: return GL_LINES;
    case PrimitiveTopology::point_list: return GL_POINTS;
    case PrimitiveTopology::patch_list: return 0;
    }
    return 0;
}

inline Status validate(const PipelineDesc& desc) {
    if (desc.rasterization.depthBiasClamp != 0.0F ||
        desc.topology == PrimitiveTopology::patch_list) {
        return Status::failure(StatusCode::unsupported,
                               "EGL/GL clamped depth bias and patches are unsupported");
    }
    if (!desc.specializationConstants.empty()) {
        return Status::failure(StatusCode::unsupported,
                               "native GLSL specialization requires a compiled variant");
    }
#if defined(TRUFFLE_EGL_API_OPENGLES)
    if (desc.rasterization.polygonMode != PolygonMode::fill ||
        desc.rasterization.depthClampEnabled ||
        desc.multisample.sampleMask != 0xffffffffu) {
        return Status::failure(StatusCode::unsupported,
                               "GLES rasterization state exceeds the core profile");
    }
    if (!desc.colorTargets.empty()) {
        const auto& first = desc.colorTargets.front();
        for (const auto& target : desc.colorTargets) {
            const auto& a = target.blend;
            const auto& b = first.blend;
            if (target.writeMask != first.writeMask || a.enabled != b.enabled ||
                a.color.sourceFactor != b.color.sourceFactor ||
                a.color.destinationFactor != b.color.destinationFactor ||
                a.color.operation != b.color.operation ||
                a.alpha.sourceFactor != b.alpha.sourceFactor ||
                a.alpha.destinationFactor != b.alpha.destinationFactor ||
                a.alpha.operation != b.alpha.operation) {
                return Status::failure(
                    StatusCode::unsupported,
                    "GLES requires common blend and write-mask state across targets");
            }
        }
    }
#endif
    return Status::success();
}

inline void viewport(const Viewport& value) {
    glViewport(static_cast<GLint>(value.x), static_cast<GLint>(value.y),
               static_cast<GLsizei>(value.width),
               static_cast<GLsizei>(value.height));
#if defined(TRUFFLE_EGL_API_OPENGL)
    glDepthRange(value.minimumDepth, value.maximumDepth);
#else
    glDepthRangef(value.minimumDepth, value.maximumDepth);
#endif
}

inline void scissor(const ScissorRect& value) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(value.x, value.y, static_cast<GLsizei>(value.width),
              static_cast<GLsizei>(value.height));
}

inline void stencil_reference(const DepthStencilState& state,
                               std::uint32_t reference) {
    for (const auto face : {GL_FRONT, GL_BACK}) {
        const auto& value = face == GL_FRONT ? state.front : state.back;
        glStencilFuncSeparate(static_cast<GLenum>(face), compare(value.compare),
                              static_cast<GLint>(reference), state.stencilReadMask);
        glStencilOpSeparate(static_cast<GLenum>(face), stencil(value.failOp),
                            stencil(value.depthFailOp), stencil(value.passOp));
        glStencilMaskSeparate(static_cast<GLenum>(face), state.stencilWriteMask);
    }
}

inline void depth_bias(float constant, float slope) {
    if (constant != 0.0F || slope != 0.0F) {
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glPolygonOffset(slope, constant);
}

inline void apply(const PipelineDesc& desc, Extent2D extent) {
    const auto& raster = desc.rasterization;
    if (raster.cullMode == CullMode::none) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(raster.cullMode == CullMode::front ? GL_FRONT : GL_BACK);
    }
    glFrontFace(raster.frontFace == FrontFace::clockwise ? GL_CW : GL_CCW);
#if defined(TRUFFLE_EGL_API_OPENGL)
    glPolygonMode(GL_FRONT_AND_BACK,
                  raster.polygonMode == PolygonMode::line ? GL_LINE :
                  raster.polygonMode == PolygonMode::point ? GL_POINT : GL_FILL);
    if (raster.depthClampEnabled) glEnable(GL_DEPTH_CLAMP);
    else glDisable(GL_DEPTH_CLAMP);
    glEnable(GL_MULTISAMPLE);
    glSampleMaski(0, desc.multisample.sampleMask);
    if (desc.multisample.sampleMask != 0xffffffffu) glEnable(GL_SAMPLE_MASK);
    else glDisable(GL_SAMPLE_MASK);
#endif
    if (desc.multisample.alphaToCoverageEnabled) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    const auto& depth = desc.depthStencil;
    if (depth.format != TextureFormat::unknown) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(compare(depth.depthCompare));
    glDepthMask(depth.depthWriteEnabled ? GL_TRUE : GL_FALSE);
    if (depth.format == TextureFormat::depth24_unorm_stencil8 ||
        depth.format == TextureFormat::depth32_float_stencil8) {
        glEnable(GL_STENCIL_TEST);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    stencil_reference(depth, desc.stencilReference);
    depth_bias(raster.depthBias, raster.depthBiasSlopeScale);
    for (std::size_t index = 0; index < desc.colorTargets.size(); ++index) {
        const auto& target = desc.colorTargets[index];
        const auto& blend = target.blend;
#if defined(TRUFFLE_EGL_API_OPENGL)
        const auto slot = static_cast<GLuint>(index);
        if (blend.enabled) glEnablei(GL_BLEND, slot);
        else glDisablei(GL_BLEND, slot);
        glBlendFuncSeparatei(slot, blend_factor(blend.color.sourceFactor),
                             blend_factor(blend.color.destinationFactor),
                             blend_factor(blend.alpha.sourceFactor),
                             blend_factor(blend.alpha.destinationFactor));
        glBlendEquationSeparatei(slot, blend_op(blend.color.operation),
                                 blend_op(blend.alpha.operation));
        glColorMaski(slot, (target.writeMask & 1) != 0, (target.writeMask & 2) != 0,
                     (target.writeMask & 4) != 0, (target.writeMask & 8) != 0);
#else
        if (blend.enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        glBlendFuncSeparate(blend_factor(blend.color.sourceFactor),
                            blend_factor(blend.color.destinationFactor),
                            blend_factor(blend.alpha.sourceFactor),
                            blend_factor(blend.alpha.destinationFactor));
        glBlendEquationSeparate(blend_op(blend.color.operation),
                                blend_op(blend.alpha.operation));
        glColorMask((target.writeMask & 1) != 0, (target.writeMask & 2) != 0,
                    (target.writeMask & 4) != 0, (target.writeMask & 8) != 0);
#endif
    }
    glBlendColor(desc.blendConstant[0], desc.blendConstant[1],
                  desc.blendConstant[2], desc.blendConstant[3]);
    if (!has_dynamic_state(desc.dynamicState, DynamicState::viewport)) {
        viewport(desc.viewports.empty()
                     ? Viewport{0, 0, static_cast<float>(extent.width),
                                static_cast<float>(extent.height), 0, 1}
                     : desc.viewports.front());
    }
    if (!has_dynamic_state(desc.dynamicState, DynamicState::scissor)) {
        if (desc.scissors.empty()) glDisable(GL_SCISSOR_TEST);
        else scissor(desc.scissors.front());
    }
}

struct VertexBinding {
    GLuint name = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct InputState {
    std::array<VertexBinding, 8> vertices{};
    GLuint indexBuffer = 0;
    std::size_t indexOffset = 0;
    std::size_t indexSize = 0;
    IndexFormat indexFormat = IndexFormat::uint16;

    Status apply(const PipelineDesc& desc) const {
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maximum);
        for (GLint index = 0; index < maximum; ++index) {
            glDisableVertexAttribArray(static_cast<GLuint>(index));
        }
        for (std::size_t slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
            const auto& layout = desc.vertexBuffers[slot];
            const auto& buffer = vertices[slot];
            if (buffer.name == 0) {
                return Status::failure(StatusCode::invalid_state,
                                       "EGL/GL vertex stream is not bound");
            }
            glBindBuffer(GL_ARRAY_BUFFER, buffer.name);
            for (const auto& attribute : layout.attributes) {
                const auto format = static_cast<unsigned>(attribute.format);
                const auto components = static_cast<GLint>(format % 4 + 1);
                const auto bytes = static_cast<std::size_t>(components) * 4;
                if (attribute.location >= static_cast<std::uint32_t>(maximum) ||
                    buffer.offset > buffer.size ||
                    attribute.offset > buffer.size - buffer.offset ||
                    bytes > buffer.size - buffer.offset - attribute.offset) {
                    return Status::failure(StatusCode::invalid_argument,
                                           "EGL/GL vertex attribute is out of range");
                }
                const auto* offset = reinterpret_cast<const void*>(
                    buffer.offset + attribute.offset);
                glEnableVertexAttribArray(attribute.location);
                if (format >= static_cast<unsigned>(VertexFormat::uint32)) {
                    glVertexAttribIPointer(attribute.location, components,
                                           GL_UNSIGNED_INT,
                                           static_cast<GLsizei>(layout.stride), offset);
                } else {
                    glVertexAttribPointer(attribute.location, components, GL_FLOAT,
                                          GL_FALSE,
                                          static_cast<GLsizei>(layout.stride), offset);
                }
                glVertexAttribDivisor(attribute.location,
                                      layout.stepMode == VertexStepMode::instance ? 1 : 0);
            }
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        return Status::success();
    }
};

} // namespace truffle::rhi::detail::egl_probe::graphics

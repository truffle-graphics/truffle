#pragma once

#include "truffle/renderer/rhi_interop.hpp"

#include <type_traits>

namespace truffle::renderer::detail {

#define TFL_RENDERER_ENUM(Type, Name)                                                    \
  static_assert(static_cast<unsigned>(Type::Name) ==                                     \
                static_cast<unsigned>(rhi::Type::Name))
TFL_RENDERER_ENUM(TextureFormat, unknown);
TFL_RENDERER_ENUM(TextureFormat, r8_unorm);
TFL_RENDERER_ENUM(TextureFormat, rg8_unorm);
TFL_RENDERER_ENUM(TextureFormat, rgba8_unorm);
TFL_RENDERER_ENUM(TextureFormat, rgba8_srgb);
TFL_RENDERER_ENUM(TextureFormat, bgra8_unorm);
TFL_RENDERER_ENUM(TextureFormat, bgra8_srgb);
TFL_RENDERER_ENUM(TextureFormat, rgba16_float);
TFL_RENDERER_ENUM(TextureFormat, rgba32_float);
TFL_RENDERER_ENUM(TextureFormat, depth16_unorm);
TFL_RENDERER_ENUM(TextureFormat, depth24_unorm_stencil8);
TFL_RENDERER_ENUM(TextureFormat, depth32_float);
TFL_RENDERER_ENUM(TextureFormat, depth32_float_stencil8);
TFL_RENDERER_ENUM(BufferUsage, none);
TFL_RENDERER_ENUM(BufferUsage, vertex);
TFL_RENDERER_ENUM(BufferUsage, index);
TFL_RENDERER_ENUM(BufferUsage, uniform);
TFL_RENDERER_ENUM(BufferUsage, storage);
TFL_RENDERER_ENUM(BufferUsage, indirect);
TFL_RENDERER_ENUM(IndexFormat, uint16);
TFL_RENDERER_ENUM(IndexFormat, uint32);
TFL_RENDERER_ENUM(VertexFormat, float32);
TFL_RENDERER_ENUM(VertexFormat, float32x2);
TFL_RENDERER_ENUM(VertexFormat, float32x3);
TFL_RENDERER_ENUM(VertexFormat, float32x4);
TFL_RENDERER_ENUM(VertexFormat, uint32);
TFL_RENDERER_ENUM(VertexFormat, uint32x2);
TFL_RENDERER_ENUM(VertexFormat, uint32x3);
TFL_RENDERER_ENUM(VertexFormat, uint32x4);
TFL_RENDERER_ENUM(VertexStepMode, vertex);
TFL_RENDERER_ENUM(VertexStepMode, instance);
TFL_RENDERER_ENUM(PrimitiveTopology, triangle_list);
TFL_RENDERER_ENUM(PrimitiveTopology, triangle_strip);
TFL_RENDERER_ENUM(PrimitiveTopology, line_list);
TFL_RENDERER_ENUM(PrimitiveTopology, point_list);
TFL_RENDERER_ENUM(PrimitiveTopology, patch_list);
TFL_RENDERER_ENUM(PolygonMode, fill);
TFL_RENDERER_ENUM(PolygonMode, line);
TFL_RENDERER_ENUM(PolygonMode, point);
TFL_RENDERER_ENUM(CullMode, none);
TFL_RENDERER_ENUM(CullMode, front);
TFL_RENDERER_ENUM(CullMode, back);
TFL_RENDERER_ENUM(FrontFace, counter_clockwise);
TFL_RENDERER_ENUM(FrontFace, clockwise);
TFL_RENDERER_ENUM(CompareOp, never);
TFL_RENDERER_ENUM(CompareOp, less);
TFL_RENDERER_ENUM(CompareOp, equal);
TFL_RENDERER_ENUM(CompareOp, less_equal);
TFL_RENDERER_ENUM(CompareOp, greater);
TFL_RENDERER_ENUM(CompareOp, not_equal);
TFL_RENDERER_ENUM(CompareOp, greater_equal);
TFL_RENDERER_ENUM(CompareOp, always);
TFL_RENDERER_ENUM(StencilOp, keep);
TFL_RENDERER_ENUM(StencilOp, zero);
TFL_RENDERER_ENUM(StencilOp, replace);
TFL_RENDERER_ENUM(StencilOp, increment_clamp);
TFL_RENDERER_ENUM(StencilOp, decrement_clamp);
TFL_RENDERER_ENUM(StencilOp, invert);
TFL_RENDERER_ENUM(StencilOp, increment_wrap);
TFL_RENDERER_ENUM(StencilOp, decrement_wrap);
TFL_RENDERER_ENUM(BlendFactor, zero);
TFL_RENDERER_ENUM(BlendFactor, one);
TFL_RENDERER_ENUM(BlendFactor, source_color);
TFL_RENDERER_ENUM(BlendFactor, one_minus_source_color);
TFL_RENDERER_ENUM(BlendFactor, destination_color);
TFL_RENDERER_ENUM(BlendFactor, one_minus_destination_color);
TFL_RENDERER_ENUM(BlendFactor, source_alpha);
TFL_RENDERER_ENUM(BlendFactor, one_minus_source_alpha);
TFL_RENDERER_ENUM(BlendFactor, destination_alpha);
TFL_RENDERER_ENUM(BlendFactor, one_minus_destination_alpha);
TFL_RENDERER_ENUM(BlendOp, add);
TFL_RENDERER_ENUM(BlendOp, subtract);
TFL_RENDERER_ENUM(BlendOp, reverse_subtract);
TFL_RENDERER_ENUM(BlendOp, minimum);
TFL_RENDERER_ENUM(BlendOp, maximum);
TFL_RENDERER_ENUM(Filter, nearest);
TFL_RENDERER_ENUM(Filter, linear);
TFL_RENDERER_ENUM(SamplerAddressMode, clamp_to_edge);
TFL_RENDERER_ENUM(SamplerAddressMode, repeat);
TFL_RENDERER_ENUM(SamplerAddressMode, mirror_repeat);
TFL_RENDERER_ENUM(LoadOp, load);
TFL_RENDERER_ENUM(LoadOp, clear);
TFL_RENDERER_ENUM(LoadOp, dont_care);
TFL_RENDERER_ENUM(StoreOp, store);
TFL_RENDERER_ENUM(StoreOp, dont_care);
TFL_RENDERER_ENUM(PresentMode, fifo);
TFL_RENDERER_ENUM(PresentMode, mailbox);
TFL_RENDERER_ENUM(PresentMode, immediate);
#undef TFL_RENDERER_ENUM

template <class To, class From> constexpr To enum_value(From value) noexcept {
  static_assert(std::is_enum_v<To> && std::is_enum_v<From>);
  return static_cast<To>(value);
}
inline bool valid_format(TextureFormat format) {
  return format >= TextureFormat::unknown &&
         format <= TextureFormat::depth32_float_stencil8;
}
inline rhi::Extent2D extent(PixelSize value) { return {value.width, value.height}; }
inline PixelSize pixels(rhi::Extent2D value) { return {value.width, value.height}; }
inline rhi::SamplerDesc sampler(const SamplerDesc &value) {
  return {
      .minFilter = enum_value<rhi::Filter>(value.minFilter),
      .magFilter = enum_value<rhi::Filter>(value.magFilter),
      .mipFilter = enum_value<rhi::Filter>(value.mipFilter),
      .addressU = enum_value<rhi::SamplerAddressMode>(value.addressU),
      .addressV = enum_value<rhi::SamplerAddressMode>(value.addressV),
      .addressW = enum_value<rhi::SamplerAddressMode>(value.addressW),
      .lodMin = value.lodMin,
      .lodMax = value.lodMax,
      .maxAnisotropy = value.maxAnisotropy,
      .compare = enum_value<rhi::CompareOp>(value.compare),
      .debugName = value.debugName,
  };
}
inline RendererFrameStats stats(const specialist::RendererFrameStats &value) {
  return {
      value.computeNodesExecuted,  value.renderNodesExecuted, value.renderBatchesExecuted,
      value.renderBatchesClipped,  value.submitted,           value.presented,
      value.presentationSuboptimal};
}
inline base::Status invalid(const char *message) {
  return base::Status::failure(base::StatusCode::invalid_state, message);
}

} // namespace truffle::renderer::detail

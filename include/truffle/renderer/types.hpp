#pragma once

#include "truffle/renderer/window.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace truffle::renderer {

enum class Backend { automatic, metal, vulkan };
enum class TextureFormat {
  unknown,
  r8_unorm,
  rg8_unorm,
  rgba8_unorm,
  rgba8_srgb,
  bgra8_unorm,
  bgra8_srgb,
  rgba16_float,
  rgba32_float,
  depth16_unorm,
  depth24_unorm_stencil8,
  depth32_float,
  depth32_float_stencil8,
};
enum class PresentMode { fifo, mailbox, immediate };
enum class IndexFormat { uint16, uint32 };
enum class BufferUsage : std::uint32_t {
  none = 0,
  vertex = 1,
  index = 2,
  uniform = 4,
  storage = 8,
  indirect = 16,
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
  return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) |
                                  static_cast<std::uint32_t>(b));
}
enum class VertexFormat {
  float32,
  float32x2,
  float32x3,
  float32x4,
  uint32,
  uint32x2,
  uint32x3,
  uint32x4,
};
enum class VertexStepMode { vertex, instance };
enum class PrimitiveTopology {
  triangle_list,
  triangle_strip,
  line_list,
  point_list,
  patch_list
};
enum class PolygonMode { fill, line, point };
enum class CullMode { none, front, back };
enum class FrontFace { counter_clockwise, clockwise };
enum class CompareOp {
  never,
  less,
  equal,
  less_equal,
  greater,
  not_equal,
  greater_equal,
  always
};
enum class StencilOp {
  keep,
  zero,
  replace,
  increment_clamp,
  decrement_clamp,
  invert,
  increment_wrap,
  decrement_wrap
};
enum class BlendFactor {
  zero,
  one,
  source_color,
  one_minus_source_color,
  destination_color,
  one_minus_destination_color,
  source_alpha,
  one_minus_source_alpha,
  destination_alpha,
  one_minus_destination_alpha,
};
enum class BlendOp { add, subtract, reverse_subtract, minimum, maximum };
enum class Filter { nearest, linear };
enum class SamplerAddressMode { clamp_to_edge, repeat, mirror_repeat };
enum class LoadOp { load, clear, dont_care };
enum class StoreOp { store, dont_care };
enum class ResourceKind { uniform_buffer, storage_buffer, sampled_texture, sampler };
enum class ShaderVisibility { vertex, fragment, graphics };

struct ClearColor {
  float r = 0, g = 0, b = 0, a = 1;
};
struct PixelOrigin {
  std::uint32_t x = 0, y = 0;
};
struct VertexAttributeDesc {
  std::uint32_t location = 0;
  VertexFormat format = VertexFormat::float32x4;
  std::uint32_t offset = 0;
};
struct VertexBufferLayoutDesc {
  std::uint32_t stride = 0;
  VertexStepMode stepMode = VertexStepMode::vertex;
  std::vector<VertexAttributeDesc> attributes;
};
struct BlendComponent {
  BlendFactor sourceFactor = BlendFactor::one;
  BlendFactor destinationFactor = BlendFactor::zero;
  BlendOp operation = BlendOp::add;
};
struct BlendState {
  bool enabled = false;
  BlendComponent color, alpha;
};
struct ColorTargetState {
  TextureFormat format = TextureFormat::bgra8_unorm;
  BlendState blend;
  std::uint8_t writeMask = 0x0f;
};
struct StencilFaceState {
  CompareOp compare = CompareOp::always;
  StencilOp failOp = StencilOp::keep, depthFailOp = StencilOp::keep,
            passOp = StencilOp::keep;
};
struct DepthStencilState {
  TextureFormat format = TextureFormat::unknown;
  bool depthWriteEnabled = false;
  CompareOp depthCompare = CompareOp::always;
  StencilFaceState front, back;
  std::uint32_t stencilReadMask = 0xffffffffu, stencilWriteMask = 0xffffffffu;
};
struct RasterizationState {
  PolygonMode polygonMode = PolygonMode::fill;
  CullMode cullMode = CullMode::none;
  FrontFace frontFace = FrontFace::counter_clockwise;
  bool depthClampEnabled = false;
  float depthBias = 0, depthBiasSlopeScale = 0, depthBiasClamp = 0;
};
struct MultisampleState {
  std::uint32_t sampleCount = 1, sampleMask = 0xffffffffu;
  bool alphaToCoverageEnabled = false;
};
struct SamplerDesc {
  Filter minFilter = Filter::nearest, magFilter = Filter::nearest,
         mipFilter = Filter::nearest;
  SamplerAddressMode addressU = SamplerAddressMode::clamp_to_edge;
  SamplerAddressMode addressV = SamplerAddressMode::clamp_to_edge;
  SamplerAddressMode addressW = SamplerAddressMode::clamp_to_edge;
  float lodMin = 0, lodMax = 32, maxAnisotropy = 1;
  CompareOp compare = CompareOp::always;
  std::string debugName;
};

} // namespace truffle::renderer

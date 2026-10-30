// Native Metal conformance evidence for issue #69. Each section proves one
// advertised capability, format, or limit with real hardware readback, or
// proves that an unsupported shape is reported as unsupported.

#include "truffle/rhi/metal_backend.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace rhi = truffle::rhi;
using truffle::base::Status;
using truffle::base::StatusCode;

constexpr std::string_view kMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FullscreenOutput {
    float4 position [[position]];
    float4 color;
};

float4 fullscreen_position(uint vertexId) {
    const float2 corner = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    return float4(corner * 2.0 - 1.0, 0.5, 1.0);
}

vertex FullscreenOutput fullscreen_vertex(uint vertexId [[vertex_id]]) {
    FullscreenOutput output;
    output.position = fullscreen_position(vertexId);
    output.color = float4(0.0);
    return output;
}

fragment float4 constant_fragment(FullscreenOutput input [[stage_in]],
                                  constant float4& color [[buffer(30)]]) {
    (void)input;
    return color;
}

fragment float4 wide_push_fragment(FullscreenOutput input [[stage_in]],
                                   constant float4* pushed [[buffer(30)]]) {
    (void)input;
    return pushed[15];
}

fragment float4 varying_fragment(FullscreenOutput input [[stage_in]]) {
    return input.color;
}

struct InstancedInput {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float offset [[attribute(2)]];
};

vertex FullscreenOutput instanced_vertex(InstancedInput input [[stage_in]]) {
    FullscreenOutput output;
    output.position = float4(input.position.x + input.offset, input.position.y,
                             0.5, 1.0);
    output.color = input.color;
    return output;
}

struct SixteenInputs {
    float a0 [[attribute(0)]];
    float a1 [[attribute(1)]];
    float a2 [[attribute(2)]];
    float a3 [[attribute(3)]];
    float a4 [[attribute(4)]];
    float a5 [[attribute(5)]];
    float a6 [[attribute(6)]];
    float a7 [[attribute(7)]];
    float a8 [[attribute(8)]];
    float a9 [[attribute(9)]];
    float a10 [[attribute(10)]];
    float a11 [[attribute(11)]];
    float a12 [[attribute(12)]];
    float a13 [[attribute(13)]];
    float a14 [[attribute(14)]];
    float a15 [[attribute(15)]];
};

vertex FullscreenOutput sixteen_vertex(SixteenInputs input [[stage_in]],
                                       uint vertexId [[vertex_id]]) {
    FullscreenOutput output;
    output.position = fullscreen_position(vertexId);
    const float sum = input.a0 + input.a1 + input.a2 + input.a3 + input.a4 +
                      input.a5 + input.a6 + input.a7 + input.a8 + input.a9 +
                      input.a10 + input.a11 + input.a12 + input.a13 +
                      input.a14 + input.a15;
    output.color = float4(sum, 0.0, 0.0, 1.0);
    return output;
}

struct ViewportOutput {
    float4 position [[position]];
    float4 color;
    uint viewport [[viewport_array_index]];
};

vertex ViewportOutput viewport_vertex(uint vertexId [[vertex_id]],
                                      uint instanceId [[instance_id]]) {
    ViewportOutput output;
    output.position = fullscreen_position(vertexId);
    output.color = float4(float(instanceId) / 15.0, 0.0, 0.0, 1.0);
    output.viewport = instanceId;
    return output;
}

fragment float4 viewport_fragment(ViewportOutput input [[stage_in]]) {
    return input.color;
}

struct EightOutputs {
    float4 c0 [[color(0)]];
    float4 c1 [[color(1)]];
    float4 c2 [[color(2)]];
    float4 c3 [[color(3)]];
    float4 c4 [[color(4)]];
    float4 c5 [[color(5)]];
    float4 c6 [[color(6)]];
    float4 c7 [[color(7)]];
};

fragment EightOutputs eight_fragment(FullscreenOutput input [[stage_in]]) {
    (void)input;
    EightOutputs output;
    output.c0 = float4(0.0 / 7.0, 0.0, 0.0, 1.0);
    output.c1 = float4(1.0 / 7.0, 0.0, 0.0, 1.0);
    output.c2 = float4(2.0 / 7.0, 0.0, 0.0, 1.0);
    output.c3 = float4(3.0 / 7.0, 0.0, 0.0, 1.0);
    output.c4 = float4(4.0 / 7.0, 0.0, 0.0, 1.0);
    output.c5 = float4(5.0 / 7.0, 0.0, 0.0, 1.0);
    output.c6 = float4(6.0 / 7.0, 0.0, 0.0, 1.0);
    output.c7 = float4(7.0 / 7.0, 0.0, 0.0, 1.0);
    return output;
}

fragment float4 sampled_fragment(FullscreenOutput input [[stage_in]],
                                 texture2d<float> source [[texture(0)]],
                                 sampler sourceSampler [[sampler(1)]]) {
    return source.sample(sourceSampler, input.position.xy / 16.0);
}

kernel void groups_kernel(constant uint4& input [[buffer(0)]],
                          device uint* dynamicOutput [[buffer(1)]],
                          device uint* out0 [[buffer(8)]],
                          device uint* out1 [[buffer(9)]],
                          device uint* out2 [[buffer(10)]],
                          device uint* out3 [[buffer(11)]],
                          device uint* out4 [[buffer(12)]],
                          device uint* out5 [[buffer(13)]],
                          device uint* out6 [[buffer(14)]],
                          device uint* out7 [[buffer(15)]]) {
    out0[0] = input.x + 0;
    out1[0] = input.x + 1;
    out2[0] = input.x + 2;
    out3[0] = input.x + 3;
    out4[0] = input.x + 4;
    out5[0] = input.x + 5;
    out6[0] = input.x + 6;
    out7[0] = input.x + 7;
    dynamicOutput[0] = input.y;
}

kernel void wide_kernel(device uint* output [[buffer(0)]],
                        uint3 local [[thread_position_in_threadgroup]],
                        uint3 groupSize [[threads_per_threadgroup]]) {
    const uint index = local.x + local.y * groupSize.x +
                       local.z * groupSize.x * groupSize.y;
    output[index] = index + 1;
}

kernel void push_kernel(device uint* output [[buffer(0)]],
                        constant uint4* pushed [[buffer(30)]]) {
    output[0] = pushed[15].w;
}
)MSL";

std::vector<std::byte> shader_bytes(std::string_view source) {
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    return {bytes.begin(), bytes.end()};
}

rhi::Shader make_shader(rhi::Device& device, rhi::ShaderStage stage,
                        std::string_view entryPoint,
                        std::vector<rhi::ResourceBinding> reflection = {},
                        std::vector<rhi::PushConstantRange> pushConstants = {},
                        rhi::Extent3D workgroup = {1, 1, 1}) {
    rhi::ShaderDesc desc;
    desc.stage = stage;
    desc.entryPoint = entryPoint;
    desc.code = shader_bytes(kMsl);
    desc.reflection = std::move(reflection);
    desc.pushConstants = std::move(pushConstants);
    desc.requiredWorkgroupSize = workgroup;
    desc.preferredWorkgroupSize = workgroup;
    auto result = device.create_shader(desc);
    assert(result.ok());
    return std::move(result).value();
}

rhi::Buffer make_buffer(rhi::Device& device, std::size_t size,
                        rhi::BufferUsage usage,
                        rhi::MemoryDomain memory = rhi::MemoryDomain::upload) {
    auto result = device.create_buffer(
        {.size = size, .usage = usage, .memory = memory});
    assert(result.ok());
    return std::move(result).value();
}

rhi::Texture make_texture(rhi::Device& device, const rhi::TextureDesc& desc) {
    auto result = device.create_texture(desc);
    assert(result.ok());
    return std::move(result).value();
}

Status submit(rhi::Device& device,
              const std::function<void(rhi::CommandList&)>& record) {
    auto pool = device.create_command_pool(rhi::QueueKind::graphics);
    assert(pool.ok());
    auto listResult = pool.value().allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    record(list);
    assert(list.end().ok());
    auto queue = device.queue(rhi::QueueKind::graphics);
    assert(queue.ok());
    std::array<rhi::CommandList*, 1> lists{&list};
    return queue.value().submit(lists);
}

Status copy(rhi::Device& device,
            const std::function<void(rhi::CopyEncoder&)>& record) {
    return submit(device, [&](rhi::CommandList& list) {
        auto encoder = list.begin_copy();
        assert(encoder.ok());
        record(encoder.value());
        assert(encoder.value().end().ok());
    });
}

void render(rhi::Device& device, const rhi::RenderPassDesc& pass,
            const std::function<void(rhi::RenderEncoder&)>& record) {
    const auto status = submit(device, [&](rhi::CommandList& list) {
        auto encoder = list.begin_rendering(pass);
        assert(encoder.ok());
        record(encoder.value());
        assert(encoder.value().end().ok());
    });
    assert(status.ok());
}

void compute(rhi::Device& device,
             const std::function<void(rhi::ComputeEncoder&)>& record) {
    const auto status = submit(device, [&](rhi::CommandList& list) {
        auto encoder = list.begin_compute();
        assert(encoder.ok());
        record(encoder.value());
        assert(encoder.value().end().ok());
    });
    assert(status.ok());
}

constexpr std::size_t kRowPitch = 256;

rhi::TextureRegion whole(std::uint32_t width, std::uint32_t height,
                         rhi::TextureAspect aspect = rhi::TextureAspect::color,
                         std::uint32_t mip = 0) {
    return {.subresource = {.aspect = aspect, .mipLevel = mip},
            .extent = {width, height, 1}};
}

// Reads a texture region back through a padded buffer and returns tightly
// packed texels.
std::vector<std::byte> read_region(rhi::Device& device, rhi::Texture& texture,
                                   const rhi::TextureRegion& region,
                                   std::size_t bytesPerTexel) {
    auto readback = make_buffer(device, kRowPitch * region.extent.height,
                                rhi::BufferUsage::copy_destination,
                                rhi::MemoryDomain::readback);
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .copy_texture_to_buffer(
                              texture, readback,
                              {.layout = {.bytesPerRow = kRowPitch,
                                          .rowsPerImage = region.extent.height},
                               .texture = region})
                          .ok());
           }).ok());
    std::vector<std::byte> padded(kRowPitch * region.extent.height);
    assert(readback.read(0, padded).ok());
    std::vector<std::byte> packed;
    for (std::uint32_t row = 0; row < region.extent.height; ++row) {
        const auto* begin = padded.data() + row * kRowPitch;
        packed.insert(packed.end(), begin,
                      begin + region.extent.width * bytesPerTexel);
    }
    return packed;
}

void upload_region(rhi::Device& device, rhi::Texture& texture,
                   const rhi::TextureRegion& region,
                   std::span<const std::byte> packed,
                   std::size_t bytesPerTexel) {
    std::vector<std::byte> padded(kRowPitch * region.extent.height);
    for (std::uint32_t row = 0; row < region.extent.height; ++row) {
        std::memcpy(padded.data() + row * kRowPitch,
                    packed.data() + row * region.extent.width * bytesPerTexel,
                    region.extent.width * bytesPerTexel);
    }
    auto upload = make_buffer(device, padded.size(),
                              rhi::BufferUsage::copy_source);
    assert(upload.write(0, padded).ok());
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .copy_buffer_to_texture(
                              upload, texture,
                              {.layout = {.bytesPerRow = kRowPitch,
                                          .rowsPerImage = region.extent.height},
                               .texture = region})
                          .ok());
           }).ok());
}

std::array<std::byte, 4> rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                               std::uint8_t a) {
    return {std::byte{r}, std::byte{g}, std::byte{b}, std::byte{a}};
}

bool near_byte(std::byte actual, int expected, int tolerance = 1) {
    return std::abs(std::to_integer<int>(actual) - expected) <= tolerance;
}

bool near_rgba8(std::span<const std::byte> texel, int r, int g, int b, int a,
                int tolerance = 1) {
    return near_byte(texel[0], r, tolerance) &&
           near_byte(texel[1], g, tolerance) &&
           near_byte(texel[2], b, tolerance) &&
           near_byte(texel[3], a, tolerance);
}

float half_to_float(std::uint16_t value) {
    const auto exponent = static_cast<int>((value >> 10u) & 31u);
    const auto mantissa = static_cast<int>(value & 1023u);
    float result = 0.0F;
    if (exponent == 0) {
        result = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        result = INFINITY;
    } else {
        result = std::ldexp(static_cast<float>(mantissa | 1024), exponent - 25);
    }
    return (value & 0x8000u) != 0 ? -result : result;
}

float linear_to_srgb(float value) {
    return value <= 0.0031308F ? value * 12.92F
                               : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

enum class Encoding { unorm, srgb, half, f32 };

struct ColorFormat {
    rhi::TextureFormat format;
    const char* name;
    std::size_t bytesPerTexel;
    std::uint32_t channels;
    Encoding encoding;
    bool bgr;
};

constexpr std::array kColorFormats{
    ColorFormat{rhi::TextureFormat::r8_unorm, "r8_unorm", 1, 1, Encoding::unorm, false},
    ColorFormat{rhi::TextureFormat::rg8_unorm, "rg8_unorm", 2, 2, Encoding::unorm, false},
    ColorFormat{rhi::TextureFormat::rgba8_unorm, "rgba8_unorm", 4, 4, Encoding::unorm, false},
    ColorFormat{rhi::TextureFormat::rgba8_srgb, "rgba8_srgb", 4, 4, Encoding::srgb, false},
    ColorFormat{rhi::TextureFormat::bgra8_unorm, "bgra8_unorm", 4, 4, Encoding::unorm, true},
    ColorFormat{rhi::TextureFormat::bgra8_srgb, "bgra8_srgb", 4, 4, Encoding::srgb, true},
    ColorFormat{rhi::TextureFormat::rgba16_float, "rgba16_float", 8, 4, Encoding::half, false},
    ColorFormat{rhi::TextureFormat::rgba32_float, "rgba32_float", 16, 4, Encoding::f32, false},
};

// Decodes one stored texel into RGBA channel values: 0-255 units for 8-bit
// formats and real values for float formats.
std::array<float, 4> decode(const ColorFormat& format, const std::byte* texel) {
    std::array<float, 4> result{};
    for (std::uint32_t channel = 0; channel < format.channels; ++channel) {
        switch (format.encoding) {
        case Encoding::unorm:
        case Encoding::srgb:
            result[channel] = std::to_integer<int>(texel[channel]);
            break;
        case Encoding::half: {
            std::uint16_t bits = 0;
            std::memcpy(&bits, texel + channel * 2, sizeof(bits));
            result[channel] = half_to_float(bits);
            break;
        }
        case Encoding::f32:
            std::memcpy(&result[channel], texel + channel * 4, sizeof(float));
            break;
        }
    }
    if (format.bgr) {
        std::swap(result[0], result[2]);
    }
    return result;
}

bool matches(const ColorFormat& format, const std::byte* texel,
             const std::array<float, 4>& linear) {
    const auto actual = decode(format, texel);
    for (std::uint32_t channel = 0; channel < format.channels; ++channel) {
        float expected = linear[channel];
        float tolerance = 1e-3F;
        if (format.encoding == Encoding::unorm ||
            format.encoding == Encoding::srgb) {
            if (format.encoding == Encoding::srgb && channel < 3) {
                expected = linear_to_srgb(expected);
            }
            expected = std::round(expected * 255.0F);
            tolerance = 1.0F;
        }
        if (std::fabs(actual[channel] - expected) > tolerance) {
            std::fprintf(stderr, "%s channel %u: %f != %f\n", format.name,
                         channel, actual[channel], expected);
            return false;
        }
    }
    return true;
}

struct Fixture {
    rhi::Device& device;
    rhi::PipelineLayout& pushLayout;
    rhi::Shader& fullscreen;
    rhi::Shader& constantColor;
};

rhi::Pipeline make_constant_pipeline(
    Fixture& fixture, std::vector<rhi::ColorTargetState> targets,
    rhi::DepthStencilState depthStencil = {},
    rhi::DynamicState dynamic = rhi::DynamicState::none,
    std::uint32_t sampleCount = 1) {
    auto result = fixture.device.create_pipeline({
        .vertexShader = &fixture.fullscreen,
        .fragmentShader = &fixture.constantColor,
        .layout = &fixture.pushLayout,
        .colorTargets = std::move(targets),
        .depthStencil = depthStencil,
        .multisample = {.sampleCount = sampleCount},
        .dynamicState = dynamic,
    });
    assert(result.ok());
    return std::move(result).value();
}

void push_color(rhi::RenderEncoder& encoder, const std::array<float, 4>& color) {
    assert(encoder
               .push_constants(rhi::ShaderStageMask::fragment, 0,
                               std::as_bytes(std::span{color}))
               .ok());
}

void test_capabilities(const rhi::Adapter& adapter) {
    const auto info = adapter.info();
    assert(info.resources.textureClear);
    assert(info.resources.textureResolve);
    assert(info.resources.textureBlitNearest);
    assert(info.resources.textureBlitLinear);
    assert(info.resources.hostCoherent);
    assert(!info.resources.externalImport && !info.resources.externalExport);
    assert(!info.bindings.bindlessTables && !info.bindings.updateAfterBind);
    assert(info.bindings.maxBindGroups == 2);
    assert(info.bindings.maxBindingsPerGroup == 8);
    assert(info.bindings.maxDescriptorsPerGroup == 8);
    assert(info.bindings.maxPushConstantBytes == 256);
    assert(info.bindings.minUniformBufferOffsetAlignment == 256);
    assert(info.bindings.minStorageBufferOffsetAlignment == 16);
    assert(info.pipelines.maxColorAttachments == 8);
    assert(info.pipelines.maxVertexBuffers == 16);
    assert(info.pipelines.maxViewports == 16);
    assert(info.pipelines.maxComputeInvocations == 1024);
    assert(!info.pipelines.tessellation && !info.pipelines.indirectCount &&
           !info.pipelines.pipelineCache);
}

// Every advertised color format must round-trip bytes through a device-local
// texture, render a known color, and clear to the same known color.
void test_color_formats(Fixture& fixture) {
    auto& device = fixture.device;
    const std::array<float, 4> color{0.25F, 0.5F, 0.75F, 1.0F};
    for (const auto& format : kColorFormats) {
        const auto usage = rhi::TextureUsage::color_attachment |
                           rhi::TextureUsage::copy_source |
                           rhi::TextureUsage::copy_destination;
        auto roundTrip = make_texture(
            device, {.extent = {4, 4, 1}, .format = format.format, .usage = usage});
        std::vector<std::byte> pattern(16 * format.bytesPerTexel);
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            // Finite, normal values for float formats; any bytes for unorm.
            pattern[index] = std::byte{static_cast<unsigned char>(
                format.encoding == Encoding::half || format.encoding == Encoding::f32
                    ? (index % 2 == 0 ? 0x10 + index % 7 : 0x3c)
                    : index * 7 + 3)};
        }
        upload_region(device, roundTrip, whole(4, 4), pattern,
                      format.bytesPerTexel);
        assert(read_region(device, roundTrip, whole(4, 4),
                           format.bytesPerTexel) == pattern);

        auto rendered = make_texture(
            device, {.extent = {4, 4, 1}, .format = format.format, .usage = usage});
        auto pipeline = make_constant_pipeline(fixture, {{.format = format.format}});
        render(device,
               {.extent = {4, 4},
                .colorAttachments = {{.texture = &rendered,
                                      .clear = {0.0F, 0.0F, 0.0F, 0.0F}}}},
               [&](rhi::RenderEncoder& encoder) {
                   assert(encoder.bind_pipeline(pipeline).ok());
                   push_color(encoder, color);
                   assert(encoder.draw(3).ok());
               });
        const auto renderedBytes =
            read_region(device, rendered, whole(4, 4), format.bytesPerTexel);
        assert(matches(format, renderedBytes.data() + 5 * format.bytesPerTexel,
                       color));

        auto cleared = make_texture(
            device, {.extent = {4, 4, 1}, .format = format.format, .usage = usage});
        assert(copy(device, [&](rhi::CopyEncoder& encoder) {
                   assert(encoder
                              .clear_texture(cleared, whole(4, 4),
                                             {.color = {color[0], color[1],
                                                        color[2], color[3]}})
                              .ok());
               }).ok());
        const auto clearedBytes =
            read_region(device, cleared, whole(4, 4), format.bytesPerTexel);
        assert(matches(format, clearedBytes.data(), color));
        assert(matches(format,
                       clearedBytes.data() + 15 * format.bytesPerTexel, color));
    }
}

float read_float(std::span<const std::byte> bytes, std::size_t index) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
    return value;
}

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t index) {
    std::uint16_t value = 0;
    std::memcpy(&value, bytes.data() + index * sizeof(value), sizeof(value));
    return value;
}

struct DepthFormat {
    rhi::TextureFormat format;
    rhi::TextureAspect aspects;
    bool unorm16;
};

constexpr std::array kDepthFormats{
    DepthFormat{rhi::TextureFormat::depth16_unorm, rhi::TextureAspect::depth, true},
    DepthFormat{rhi::TextureFormat::depth32_float, rhi::TextureAspect::depth, false},
    DepthFormat{rhi::TextureFormat::depth32_float_stencil8,
                rhi::TextureAspect::depth | rhi::TextureAspect::stencil, false},
};

float read_depth(rhi::Device& device, rhi::Texture& texture,
                 const DepthFormat& format, std::uint32_t x, std::uint32_t y) {
    const auto bytes = read_region(device, texture,
                                   whole(16, 16, rhi::TextureAspect::depth),
                                   format.unorm16 ? 2 : 4);
    const auto index = static_cast<std::size_t>(y) * 16 + x;
    return format.unorm16 ? read_u16(bytes, index) / 65535.0F
                          : read_float(bytes, index);
}

// Depth formats: upload/readback per aspect, transfer clears, and a depth
// test that keeps the first of two equal-depth draws.
void test_depth_formats(Fixture& fixture) {
    auto& device = fixture.device;
    for (const auto& format : kDepthFormats) {
        const bool stencil = rhi::has_aspect(format.aspects,
                                             rhi::TextureAspect::stencil);
        auto texture = make_texture(
            device, {.extent = {16, 16, 1},
                     .format = format.format,
                     .usage = rhi::TextureUsage::depth_stencil_attachment |
                              rhi::TextureUsage::copy_source |
                              rhi::TextureUsage::copy_destination});

        std::vector<std::byte> depthBytes(16 * 16 * (format.unorm16 ? 2 : 4));
        for (std::size_t index = 0; index < 256; ++index) {
            if (format.unorm16) {
                const auto value = static_cast<std::uint16_t>(index * 251);
                std::memcpy(depthBytes.data() + index * 2, &value, 2);
            } else {
                const auto value = static_cast<float>(index) / 256.0F;
                std::memcpy(depthBytes.data() + index * 4, &value, 4);
            }
        }
        const auto depthRegion = whole(16, 16, rhi::TextureAspect::depth);
        upload_region(device, texture, depthRegion, depthBytes,
                      format.unorm16 ? 2 : 4);
        assert(read_region(device, texture, depthRegion,
                           format.unorm16 ? 2 : 4) == depthBytes);
        if (stencil) {
            std::vector<std::byte> stencilBytes(256);
            for (std::size_t index = 0; index < stencilBytes.size(); ++index) {
                stencilBytes[index] = std::byte{static_cast<unsigned char>(index)};
            }
            const auto stencilRegion =
                whole(16, 16, rhi::TextureAspect::stencil);
            upload_region(device, texture, stencilRegion, stencilBytes, 1);
            assert(read_region(device, texture, stencilRegion, 1) ==
                   stencilBytes);
            // Copying both aspects at once has no packed Metal layout.
            auto combined = make_buffer(device, kRowPitch * 16,
                                        rhi::BufferUsage::copy_destination,
                                        rhi::MemoryDomain::readback);
            const auto combinedStatus = copy(device, [&](rhi::CopyEncoder& encoder) {
                assert(encoder
                           .copy_texture_to_buffer(
                               texture, combined,
                               {.layout = {.bytesPerRow = kRowPitch,
                                           .rowsPerImage = 16},
                                .texture = whole(16, 16, format.aspects)})
                           .ok());
            });
            assert(combinedStatus.code == StatusCode::unsupported);
        }

        assert(copy(device, [&](rhi::CopyEncoder& encoder) {
                   assert(encoder
                              .clear_texture(
                                  texture,
                                  whole(16, 16, rhi::TextureAspect::depth),
                                  {.depth = 0.25F})
                              .ok());
               }).ok());
        assert(std::fabs(read_depth(device, texture, format, 3, 9) - 0.25F) <
               1e-4F);
        if (stencil) {
            assert(copy(device, [&](rhi::CopyEncoder& encoder) {
                       assert(encoder
                                  .clear_texture(
                                      texture,
                                      whole(16, 16, rhi::TextureAspect::stencil),
                                      {.stencil = 0x5a})
                                  .ok());
                   }).ok());
            const auto stencilBytes = read_region(
                device, texture, whole(16, 16, rhi::TextureAspect::stencil), 1);
            assert(stencilBytes[0] == std::byte{0x5a} &&
                   stencilBytes[255] == std::byte{0x5a});
            // The stencil-only clear must preserve the depth aspect.
            assert(std::fabs(read_depth(device, texture, format, 3, 9) -
                             0.25F) < 1e-4F);
        }

        auto color = make_texture(
            device, {.extent = {16, 16, 1},
                     .format = rhi::TextureFormat::rgba8_unorm,
                     .usage = rhi::TextureUsage::color_attachment |
                              rhi::TextureUsage::copy_source});
        auto pipeline = make_constant_pipeline(
            fixture, {{.format = rhi::TextureFormat::rgba8_unorm}},
            {.format = format.format,
             .depthWriteEnabled = true,
             .depthCompare = rhi::CompareOp::less},
            rhi::DynamicState::viewport);
        render(device,
               {.extent = {16, 16},
                .colorAttachments = {{.texture = &color}},
                .depthStencilAttachment = {.texture = &texture,
                                           .clearDepth = 0.75F}},
               [&](rhi::RenderEncoder& encoder) {
                   assert(encoder.bind_pipeline(pipeline).ok());
                   const std::array<rhi::Viewport, 1> left{
                       {{.x = 0, .y = 0, .width = 8, .height = 16}}};
                   const std::array<rhi::Viewport, 1> full{
                       {{.x = 0, .y = 0, .width = 16, .height = 16}}};
                   assert(encoder.set_viewports(0, left).ok());
                   push_color(encoder, {1.0F, 0.0F, 0.0F, 1.0F});
                   assert(encoder.draw(3).ok());
                   assert(encoder.set_viewports(0, full).ok());
                   push_color(encoder, {0.0F, 1.0F, 0.0F, 1.0F});
                   assert(encoder.draw(3).ok());
               });
        const auto pixels = read_region(device, color, whole(16, 16), 4);
        assert(near_rgba8(std::span{pixels}.subspan((8 * 16 + 2) * 4, 4), 255,
                          0, 0, 255));
        assert(near_rgba8(std::span{pixels}.subspan((8 * 16 + 13) * 4, 4), 0,
                          255, 0, 255));
        assert(std::fabs(read_depth(device, texture, format, 2, 8) - 0.5F) <
               1e-3F);
        assert(std::fabs(read_depth(device, texture, format, 13, 8) - 0.5F) <
               1e-3F);
    }
}

// Stencil write (replace through a left-half viewport) then stencil-equal
// test, proven through color and stencil-aspect readback.
void test_stencil(Fixture& fixture) {
    auto& device = fixture.device;
    auto depthStencil = make_texture(
        device, {.extent = {16, 16, 1},
                 .format = rhi::TextureFormat::depth32_float_stencil8,
                 .usage = rhi::TextureUsage::depth_stencil_attachment |
                          rhi::TextureUsage::copy_source});
    auto color = make_texture(
        device, {.extent = {16, 16, 1},
                 .format = rhi::TextureFormat::rgba8_unorm,
                 .usage = rhi::TextureUsage::color_attachment |
                          rhi::TextureUsage::copy_source});
    const rhi::StencilFaceState write{.compare = rhi::CompareOp::always,
                                      .passOp = rhi::StencilOp::replace};
    const rhi::StencilFaceState test{.compare = rhi::CompareOp::equal};
    const auto dynamic = rhi::DynamicState::viewport |
                         rhi::DynamicState::stencil_reference;
    auto writePipeline = make_constant_pipeline(
        fixture, {{.format = rhi::TextureFormat::rgba8_unorm, .writeMask = 0}},
        {.format = rhi::TextureFormat::depth32_float_stencil8,
         .front = write,
         .back = write},
        dynamic);
    auto testPipeline = make_constant_pipeline(
        fixture, {{.format = rhi::TextureFormat::rgba8_unorm}},
        {.format = rhi::TextureFormat::depth32_float_stencil8,
         .front = test,
         .back = test},
        dynamic);
    render(device,
           {.extent = {16, 16},
            .colorAttachments = {{.texture = &color}},
            .depthStencilAttachment = {.texture = &depthStencil,
                                       .clearStencil = 0}},
           [&](rhi::RenderEncoder& encoder) {
               const std::array<rhi::Viewport, 1> left{
                   {{.x = 0, .y = 0, .width = 8, .height = 16}}};
               const std::array<rhi::Viewport, 1> full{
                   {{.x = 0, .y = 0, .width = 16, .height = 16}}};
               assert(encoder.bind_pipeline(writePipeline).ok());
               assert(encoder.set_viewports(0, left).ok());
               assert(encoder.set_stencil_reference(7).ok());
               push_color(encoder, {0.0F, 0.0F, 1.0F, 1.0F});
               assert(encoder.draw(3).ok());
               assert(encoder.bind_pipeline(testPipeline).ok());
               assert(encoder.set_viewports(0, full).ok());
               assert(encoder.set_stencil_reference(7).ok());
               push_color(encoder, {1.0F, 0.0F, 0.0F, 1.0F});
               assert(encoder.draw(3).ok());
           });
    const auto pixels = read_region(device, color, whole(16, 16), 4);
    assert(near_rgba8(std::span{pixels}.subspan((4 * 16 + 3) * 4, 4), 255, 0,
                      0, 255));
    assert(near_rgba8(std::span{pixels}.subspan((4 * 16 + 12) * 4, 4), 0, 0, 0,
                      255));
    const auto stencil = read_region(
        device, depthStencil, whole(16, 16, rhi::TextureAspect::stencil), 1);
    assert(stencil[4 * 16 + 3] == std::byte{7});
    assert(stencil[4 * 16 + 12] == std::byte{0});
}

// Mip levels: per-level upload/readback, per-level clear isolation, and blits
// that read from and write to non-zero levels.
void test_mip_levels(Fixture& fixture) {
    auto& device = fixture.device;
    const auto usage = rhi::TextureUsage::copy_source |
                       rhi::TextureUsage::copy_destination |
                       rhi::TextureUsage::sampled;
    auto texture = make_texture(device, {.extent = {8, 8, 1},
                                         .format = rhi::TextureFormat::rgba8_unorm,
                                         .usage = usage,
                                         .mipLevels = 4});
    for (std::uint32_t level = 0; level < 4; ++level) {
        const auto size = 8u >> level;
        std::vector<std::byte> bytes(size * size * 4,
                                     std::byte{static_cast<unsigned char>(
                                         0x10 * (level + 1))});
        upload_region(device, texture, whole(size, size, rhi::TextureAspect::color, level),
                      bytes, 4);
    }
    for (std::uint32_t level = 0; level < 4; ++level) {
        const auto size = 8u >> level;
        const auto bytes = read_region(
            device, texture, whole(size, size, rhi::TextureAspect::color, level), 4);
        for (const auto value : bytes) {
            assert(value == std::byte{static_cast<unsigned char>(0x10 * (level + 1))});
        }
    }
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .clear_texture(
                              texture,
                              whole(2, 2, rhi::TextureAspect::color, 2),
                              {.color = {0.0F, 1.0F, 0.0F, 1.0F}})
                          .ok());
           }).ok());
    const auto level2 =
        read_region(device, texture, whole(2, 2, rhi::TextureAspect::color, 2), 4);
    assert(near_rgba8(std::span{level2}.subspan(12, 4), 0, 255, 0, 255, 0));
    const auto level1 =
        read_region(device, texture, whole(4, 4, rhi::TextureAspect::color, 1), 4);
    assert(level1[0] == std::byte{0x20} && level1[63] == std::byte{0x20});

    // Blit level 1 (4x4 of 0x20) of one texture into level 2 of another.
    auto destination = make_texture(
        device, {.extent = {8, 8, 1},
                 .format = rhi::TextureFormat::rgba8_unorm,
                 .usage = usage,
                 .mipLevels = 3});
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .blit_texture(
                              texture, destination,
                              {.source = whole(4, 4, rhi::TextureAspect::color, 1),
                               .destination =
                                   whole(2, 2, rhi::TextureAspect::color, 2),
                               .filter = rhi::Filter::linear})
                          .ok());
           }).ok());
    const auto blitted = read_region(
        device, destination, whole(2, 2, rhi::TextureAspect::color, 2), 4);
    for (std::size_t texel = 0; texel < 4; ++texel) {
        assert(near_rgba8(std::span{blitted}.subspan(texel * 4, 4), 0x20, 0x20,
                          0x20, 0x20));
    }
}

// Clear, resolve, and blit transfers, including their unsupported shapes.
void test_texture_transfers(Fixture& fixture) {
    auto& device = fixture.device;
    const auto usage = rhi::TextureUsage::copy_source |
                       rhi::TextureUsage::copy_destination;
    auto cleared = make_texture(device, {.extent = {16, 16, 1},
                                         .format = rhi::TextureFormat::rgba8_unorm,
                                         .usage = usage});
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .clear_texture(cleared, whole(16, 16),
                                         {.color = {1.0F, 0.5F, 0.0F, 1.0F}})
                          .ok());
           }).ok());
    const auto clearedPixels = read_region(device, cleared, whole(16, 16), 4);
    assert(near_rgba8(std::span{clearedPixels}.subspan(0, 4), 255, 128, 0, 255));
    assert(near_rgba8(std::span{clearedPixels}.subspan(255 * 4, 4), 255, 128, 0,
                      255));
    const auto partialClear = copy(device, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .clear_texture(cleared,
                                  {.origin = {4, 4, 0}, .extent = {8, 8, 1}},
                                  {.color = {0.0F, 0.0F, 1.0F, 1.0F}})
                   .ok());
    });
    assert(partialClear.code == StatusCode::unsupported);

    // Resolve a render-pass-cleared multisample texture, then a transfer-
    // cleared one, into a single-sample destination.
    auto multisample = make_texture(
        device, {.extent = {16, 16, 1},
                 .format = rhi::TextureFormat::rgba8_unorm,
                 .usage = rhi::TextureUsage::color_attachment | usage,
                 .sampleCount = 4});
    auto resolved = make_texture(device, {.extent = {16, 16, 1},
                                          .format = rhi::TextureFormat::rgba8_unorm,
                                          .usage = usage});
    render(device,
           {.extent = {16, 16},
            .colorAttachments = {{.texture = &multisample,
                                  .clear = {0.0F, 1.0F, 0.0F, 1.0F}}}},
           [](rhi::RenderEncoder&) {});
    const rhi::TextureCopyRegion resolveRegion{.source = whole(16, 16),
                                               .destination = whole(16, 16)};
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder.resolve_texture(multisample, resolved, resolveRegion)
                          .ok());
           }).ok());
    auto resolvedPixels = read_region(device, resolved, whole(16, 16), 4);
    assert(near_rgba8(std::span{resolvedPixels}.subspan(17 * 4, 4), 0, 255, 0,
                      255, 0));
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .clear_texture(multisample, whole(16, 16),
                                         {.color = {1.0F, 0.0F, 0.0F, 1.0F}})
                          .ok());
               assert(encoder.resolve_texture(multisample, resolved, resolveRegion)
                          .ok());
           }).ok());
    resolvedPixels = read_region(device, resolved, whole(16, 16), 4);
    assert(near_rgba8(std::span{resolvedPixels}.subspan(200 * 4, 4), 255, 0, 0,
                      255, 0));
    const auto partialResolve = copy(device, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .resolve_texture(multisample, resolved,
                                    {.source = {.extent = {8, 8, 1}},
                                     .destination = {.extent = {8, 8, 1}}})
                   .ok());
    });
    assert(partialResolve.code == StatusCode::unsupported);

    // 2x2 source with distinct texels scaled to 4x4: nearest replicates,
    // linear blends neighbors.
    auto source = make_texture(device, {.extent = {2, 2, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = usage});
    const std::array<std::array<std::byte, 4>, 4> texels{
        rgba8(255, 0, 0, 255), rgba8(0, 255, 0, 255), rgba8(0, 0, 255, 255),
        rgba8(255, 255, 255, 255)};
    std::vector<std::byte> sourceBytes;
    for (const auto& texel : texels) {
        sourceBytes.insert(sourceBytes.end(), texel.begin(), texel.end());
    }
    upload_region(device, source, whole(2, 2), sourceBytes, 4);
    for (const auto filter : {rhi::Filter::nearest, rhi::Filter::linear}) {
        auto scaled = make_texture(device, {.extent = {4, 4, 1},
                                            .format = rhi::TextureFormat::rgba8_unorm,
                                            .usage = usage});
        assert(copy(device, [&](rhi::CopyEncoder& encoder) {
                   assert(encoder
                              .blit_texture(source, scaled,
                                            {.source = whole(2, 2),
                                             .destination = whole(4, 4),
                                             .filter = filter})
                              .ok());
               }).ok());
        const auto pixels = read_region(device, scaled, whole(4, 4), 4);
        const auto at = [&](std::uint32_t x, std::uint32_t y) {
            return std::span{pixels}.subspan((y * 4 + x) * 4, 4);
        };
        assert(near_rgba8(at(0, 0), 255, 0, 0, 255, 0));
        assert(near_rgba8(at(3, 0), 0, 255, 0, 255, 0));
        assert(near_rgba8(at(0, 3), 0, 0, 255, 255, 0));
        assert(near_rgba8(at(3, 3), 255, 255, 255, 255, 0));
        if (filter == rhi::Filter::nearest) {
            assert(near_rgba8(at(1, 0), 255, 0, 0, 255, 0));
            assert(near_rgba8(at(2, 0), 0, 255, 0, 255, 0));
        } else {
            assert(near_rgba8(at(1, 0), 191, 64, 0, 255, 2));
            assert(near_rgba8(at(2, 0), 64, 191, 0, 255, 2));
        }
    }

    // A blit into a sub-rectangle must preserve the rest of the destination.
    auto framed = make_texture(device, {.extent = {8, 8, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = usage});
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder
                          .clear_texture(framed, whole(8, 8),
                                         {.color = {0.0F, 0.0F, 0.0F, 1.0F}})
                          .ok());
               assert(encoder
                          .blit_texture(
                              source, framed,
                              {.source = {.origin = {1, 1, 0}, .extent = {1, 1, 1}},
                               .destination = {.origin = {4, 2, 0},
                                               .extent = {3, 4, 1}},
                               .filter = rhi::Filter::nearest})
                          .ok());
           }).ok());
    const auto framedPixels = read_region(device, framed, whole(8, 8), 4);
    const auto framedAt = [&](std::uint32_t x, std::uint32_t y) {
        return std::span{framedPixels}.subspan((y * 8 + x) * 4, 4);
    };
    assert(near_rgba8(framedAt(4, 2), 255, 255, 255, 255, 0));
    assert(near_rgba8(framedAt(6, 5), 255, 255, 255, 255, 0));
    assert(near_rgba8(framedAt(3, 2), 0, 0, 0, 255, 0));
    assert(near_rgba8(framedAt(7, 2), 0, 0, 0, 255, 0));
    assert(near_rgba8(framedAt(4, 1), 0, 0, 0, 255, 0));
    assert(near_rgba8(framedAt(4, 6), 0, 0, 0, 255, 0));

    // Float formats blit with linear filtering (rgba32f needs float filtering).
    for (const auto format : {rhi::TextureFormat::rgba16_float,
                              rhi::TextureFormat::rgba32_float}) {
        const ColorFormat& info =
            format == rhi::TextureFormat::rgba16_float ? kColorFormats[6]
                                                       : kColorFormats[7];
        auto floatSource = make_texture(
            device, {.extent = {2, 2, 1}, .format = format, .usage = usage});
        auto floatDestination = make_texture(
            device, {.extent = {4, 4, 1}, .format = format, .usage = usage});
        const std::array<float, 4> value{0.25F, 0.5F, 0.75F, 1.0F};
        assert(copy(device, [&](rhi::CopyEncoder& encoder) {
                   assert(encoder
                              .clear_texture(floatSource, whole(2, 2),
                                             {.color = {value[0], value[1],
                                                        value[2], value[3]}})
                              .ok());
                   assert(encoder
                              .blit_texture(floatSource, floatDestination,
                                            {.source = whole(2, 2),
                                             .destination = whole(4, 4),
                                             .filter = rhi::Filter::linear})
                              .ok());
               }).ok());
        const auto pixels = read_region(device, floatDestination, whole(4, 4),
                                        info.bytesPerTexel);
        assert(matches(info, pixels.data() + 5 * info.bytesPerTexel, value));
    }

    // Depth blits have no honest draw-based path and stay unsupported.
    auto depthSource = make_texture(device, {.extent = {4, 4, 1},
                                             .format = rhi::TextureFormat::depth32_float,
                                             .usage = usage});
    auto depthDestination = make_texture(
        device, {.extent = {4, 4, 1},
                 .format = rhi::TextureFormat::depth32_float,
                 .usage = usage});
    const auto depthBlit = copy(device, [&](rhi::CopyEncoder& encoder) {
        assert(encoder
                   .blit_texture(depthSource, depthDestination,
                                 {.source = whole(4, 4, rhi::TextureAspect::depth),
                                  .destination =
                                      whole(4, 4, rhi::TextureAspect::depth)})
                   .ok());
    });
    assert(depthBlit.code == StatusCode::unsupported);
}

// Two vertex buffers: per-vertex positions and per-instance color/offset.
void test_vertex_buffers(Fixture& fixture) {
    auto& device = fixture.device;
    auto vertexShader = make_shader(device, rhi::ShaderStage::vertex,
                                    "instanced_vertex");
    auto fragmentShader = make_shader(device, rhi::ShaderStage::fragment,
                                      "varying_fragment");
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .vertexBuffers = {
            {.stride = 8,
             .stepMode = rhi::VertexStepMode::vertex,
             .attributes = {{.location = 0,
                             .format = rhi::VertexFormat::float32x2}}},
            {.stride = 32,
             .stepMode = rhi::VertexStepMode::instance,
             .attributes = {{.location = 1,
                             .format = rhi::VertexFormat::float32x4,
                             .offset = 0},
                            {.location = 2,
                             .format = rhi::VertexFormat::float32,
                             .offset = 16}}},
        },
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    // Left-half quad in clip space; instance 1 shifts it right by 1.0.
    const std::array<float, 12> positions{-1, -1, 0, -1, -1, 1,
                                          -1, 1,  0, -1, 0,  1};
    const std::array<float, 16> instances{1, 0, 0, 1, 0, 0, 0, 0,
                                          0, 1, 0, 1, 1, 0, 0, 0};
    auto positionBuffer = make_buffer(device, sizeof(positions),
                                      rhi::BufferUsage::vertex);
    auto instanceBuffer = make_buffer(device, sizeof(instances),
                                      rhi::BufferUsage::vertex);
    assert(positionBuffer.write(0, std::as_bytes(std::span{positions})).ok());
    assert(instanceBuffer.write(0, std::as_bytes(std::span{instances})).ok());
    auto target = make_texture(device, {.extent = {16, 16, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = rhi::TextureUsage::color_attachment |
                                                 rhi::TextureUsage::copy_source});
    render(device,
           {.extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(pipeline).ok());
               assert(encoder.bind_vertex_buffer(0, positionBuffer).ok());
               assert(encoder.bind_vertex_buffer(1, instanceBuffer).ok());
               assert(encoder.draw(6, 2).ok());
           });
    const auto pixels = read_region(device, target, whole(16, 16), 4);
    assert(near_rgba8(std::span{pixels}.subspan((8 * 16 + 3) * 4, 4), 255, 0, 0,
                      255, 0));
    assert(near_rgba8(std::span{pixels}.subspan((8 * 16 + 12) * 4, 4), 0, 255,
                      0, 255, 0));

    // All sixteen advertised vertex-buffer slots feed one draw.
    auto sixteenShader = make_shader(device, rhi::ShaderStage::vertex,
                                     "sixteen_vertex");
    std::vector<rhi::VertexBufferLayoutDesc> layouts;
    std::vector<rhi::Buffer> buffers;
    for (std::uint32_t slot = 0; slot < 16; ++slot) {
        layouts.push_back({.stride = 4,
                           .attributes = {{.location = slot,
                                           .format = rhi::VertexFormat::float32}}});
        const auto value = static_cast<float>(slot + 1) / 136.0F;
        const std::array<float, 3> data{value, value, value};
        buffers.push_back(make_buffer(device, sizeof(data), rhi::BufferUsage::vertex));
        assert(buffers.back().write(0, std::as_bytes(std::span{data})).ok());
    }
    auto sixteenResult = device.create_pipeline({
        .vertexShader = &sixteenShader,
        .fragmentShader = &fragmentShader,
        .vertexBuffers = layouts,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(sixteenResult.ok());
    auto sixteen = std::move(sixteenResult).value();
    layouts.push_back(layouts.back());
    auto tooMany = device.create_pipeline({
        .vertexShader = &sixteenShader,
        .fragmentShader = &fragmentShader,
        .vertexBuffers = layouts,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(!tooMany.ok());
    render(device,
           {.extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(sixteen).ok());
               for (std::uint32_t slot = 0; slot < 16; ++slot) {
                   assert(encoder.bind_vertex_buffer(slot, buffers[slot]).ok());
               }
               assert(encoder.draw(3).ok());
           });
    const auto summed = read_region(device, target, whole(16, 16), 4);
    assert(near_rgba8(std::span{summed}.subspan((5 * 16 + 5) * 4, 4), 255, 0, 0,
                      255));
}

// Sixteen viewports selected per instance, plus a dynamic scissor.
void test_viewports_and_scissors(Fixture& fixture) {
    auto& device = fixture.device;
    auto vertexShader = make_shader(device, rhi::ShaderStage::vertex,
                                    "viewport_vertex");
    auto fragmentShader = make_shader(device, rhi::ShaderStage::fragment,
                                      "viewport_fragment");
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &vertexShader,
        .fragmentShader = &fragmentShader,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .dynamicState = rhi::DynamicState::viewport | rhi::DynamicState::scissor,
    });
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    auto target = make_texture(device, {.extent = {16, 16, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = rhi::TextureUsage::color_attachment |
                                                 rhi::TextureUsage::copy_source});
    std::array<rhi::Viewport, 16> viewports{};
    std::array<rhi::ScissorRect, 16> scissors{};
    for (std::uint32_t index = 0; index < 16; ++index) {
        viewports[index] = {.x = static_cast<float>(index),
                            .y = 0,
                            .width = 1,
                            .height = 16};
        scissors[index] = {.x = 0, .y = 0, .width = 16, .height = 16};
    }
    // The last scissor keeps only the top half of column 15.
    scissors[15] = {.x = 0, .y = 0, .width = 16, .height = 8};
    render(device,
           {.extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(pipeline).ok());
               assert(encoder.set_viewports(0, viewports).ok());
               assert(encoder.set_scissors(0, scissors).ok());
               assert(encoder.draw(3, 16).ok());
           });
    const auto pixels = read_region(device, target, whole(16, 16), 4);
    for (std::uint32_t column = 0; column < 16; ++column) {
        assert(near_rgba8(std::span{pixels}.subspan((4 * 16 + column) * 4, 4),
                          static_cast<int>(column * 17), 0, 0, 255));
    }
    assert(near_rgba8(std::span{pixels}.subspan((12 * 16 + 14) * 4, 4), 238, 0,
                      0, 255));
    assert(near_rgba8(std::span{pixels}.subspan((12 * 16 + 15) * 4, 4), 0, 0, 0,
                      255, 0));
}

// Eight simultaneous color attachments with distinct outputs.
void test_eight_color_attachments(Fixture& fixture) {
    auto& device = fixture.device;
    auto fragmentShader = make_shader(device, rhi::ShaderStage::fragment,
                                      "eight_fragment");
    std::vector<rhi::ColorTargetState> targets(
        8, {.format = rhi::TextureFormat::rgba8_unorm});
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &fixture.fullscreen,
        .fragmentShader = &fragmentShader,
        .colorTargets = targets,
    });
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    std::vector<rhi::Texture> textures;
    rhi::RenderPassDesc pass{.extent = {16, 16}};
    for (int index = 0; index < 8; ++index) {
        textures.push_back(make_texture(
            device, {.extent = {16, 16, 1},
                     .format = rhi::TextureFormat::rgba8_unorm,
                     .usage = rhi::TextureUsage::color_attachment |
                              rhi::TextureUsage::copy_source}));
    }
    for (auto& texture : textures) {
        pass.colorAttachments.push_back({.texture = &texture});
    }
    render(device, pass, [&](rhi::RenderEncoder& encoder) {
        assert(encoder.bind_pipeline(pipeline).ok());
        assert(encoder.draw(3).ok());
    });
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const auto pixels = read_region(device, textures[index], whole(16, 16), 4);
        const auto expected =
            static_cast<int>(std::lround(static_cast<double>(index) * 255.0 / 7.0));
        assert(near_rgba8(std::span{pixels}.subspan(100 * 4, 4), expected, 0, 0,
                          255));
    }
    targets.push_back({.format = rhi::TextureFormat::rgba8_unorm});
    auto tooMany = device.create_pipeline({
        .vertexShader = &fixture.fullscreen,
        .fragmentShader = &fragmentShader,
        .colorTargets = targets,
    });
    assert(!tooMany.ok());
}

// Fixed-function source-alpha blending.
void test_blend(Fixture& fixture) {
    auto& device = fixture.device;
    auto pipeline = make_constant_pipeline(
        fixture,
        {{.format = rhi::TextureFormat::rgba8_unorm,
          .blend = {.enabled = true,
                    .color = {.sourceFactor = rhi::BlendFactor::source_alpha,
                              .destinationFactor =
                                  rhi::BlendFactor::one_minus_source_alpha}}}});
    auto target = make_texture(device, {.extent = {16, 16, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = rhi::TextureUsage::color_attachment |
                                                 rhi::TextureUsage::copy_source});
    render(device,
           {.extent = {16, 16},
            .colorAttachments = {{.texture = &target,
                                  .clear = {0.0F, 0.0F, 1.0F, 1.0F}}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(pipeline).ok());
               push_color(encoder, {1.0F, 0.0F, 0.0F, 0.5F});
               assert(encoder.draw(3).ok());
           });
    const auto pixels = read_region(device, target, whole(16, 16), 4);
    assert(near_rgba8(std::span{pixels}.subspan(40 * 4, 4), 128, 0, 128, 128));
}

// The full 256-byte push-constant range in graphics and compute, including
// partial updates at a non-zero offset.
void test_push_constants(Fixture& fixture) {
    auto& device = fixture.device;
    const rhi::PushConstantRange fragmentRange{
        .stage = rhi::ShaderStage::fragment, .offset = 0, .size = 256};
    auto layoutResult = device.create_pipeline_layout(
        {.pushConstants = {fragmentRange}});
    assert(layoutResult.ok());
    auto layout = std::move(layoutResult).value();
    auto fragmentShader = make_shader(device, rhi::ShaderStage::fragment,
                                      "wide_push_fragment", {}, {fragmentRange});
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &fixture.fullscreen,
        .fragmentShader = &fragmentShader,
        .layout = &layout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    auto target = make_texture(device, {.extent = {16, 16, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = rhi::TextureUsage::color_attachment |
                                                 rhi::TextureUsage::copy_source});
    std::array<float, 64> block{};
    for (std::size_t index = 0; index < 60; index += 4) {
        block[index] = 1.0F;
        block[index + 3] = 1.0F;
    }
    block[60] = 1.0F;
    block[63] = 1.0F;
    const std::array<float, 4> tail{0.0F, 0.0F, 1.0F, 1.0F};
    render(device,
           {.extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(pipeline).ok());
               assert(encoder
                          .push_constants(rhi::ShaderStageMask::fragment, 0,
                                          std::as_bytes(std::span{block}))
                          .ok());
               assert(encoder
                          .push_constants(rhi::ShaderStageMask::fragment, 240,
                                          std::as_bytes(std::span{tail}))
                          .ok());
               assert(encoder.draw(3).ok());
           });
    const auto pixels = read_region(device, target, whole(16, 16), 4);
    assert(near_rgba8(std::span{pixels}.subspan(77 * 4, 4), 0, 0, 255, 255, 0));
    auto oversized = device.create_pipeline_layout(
        {.pushConstants = {{.stage = rhi::ShaderStage::fragment,
                            .offset = 0,
                            .size = 260}}});
    assert(!oversized.ok());

    const rhi::PushConstantRange computeRange{
        .stage = rhi::ShaderStage::compute, .offset = 0, .size = 256};
    auto computeLayoutResult = device.create_pipeline_layout(
        {.pushConstants = {computeRange}});
    assert(computeLayoutResult.ok());
    auto computeLayout = std::move(computeLayoutResult).value();
    auto kernel = make_shader(device, rhi::ShaderStage::compute, "push_kernel",
                              {}, {computeRange});
    auto computePipelineResult = device.create_compute_pipeline(
        {.computeShader = &kernel, .layout = &computeLayout});
    assert(computePipelineResult.ok());
    auto computePipeline = std::move(computePipelineResult).value();
    auto output = make_buffer(device, 16, rhi::BufferUsage::storage);
    std::array<std::uint32_t, 64> words{};
    words[63] = 0xc0ffee;
    compute(device, [&](rhi::ComputeEncoder& encoder) {
        assert(encoder.bind_pipeline(computePipeline).ok());
        assert(encoder.bind_storage_buffer(0, output).ok());
        assert(encoder.push_constants(0, std::as_bytes(std::span{words})).ok());
        assert(encoder.dispatch(1, 1, 1).ok());
    });
    std::uint32_t pushed = 0;
    assert(output.read(0, std::as_writable_bytes(std::span{&pushed, 1})).ok());
    assert(pushed == 0xc0ffee);
}

// Two bind groups through the default group * 8 + binding mapping: group 0
// carries dynamic uniform (256) and storage (16) offsets, group 1 fills all
// eight bindings/descriptors.
void test_bind_groups(Fixture& fixture) {
    auto& device = fixture.device;
    auto group0Result = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::compute,
                     .dynamicOffset = true,
                     .minimumBufferSize = 16},
                    {.binding = 1,
                     .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute,
                     .dynamicOffset = true,
                     .minimumBufferSize = 4}},
    });
    assert(group0Result.ok());
    auto group0Layout = std::move(group0Result).value();
    rhi::BindGroupLayoutDesc group1Desc{.group = 1};
    for (std::uint32_t binding = 0; binding < 8; ++binding) {
        group1Desc.entries.push_back({.binding = binding,
                                      .type = rhi::BindingType::storage_buffer,
                                      .visibility = rhi::ShaderStageMask::compute,
                                      .minimumBufferSize = 4});
    }
    auto group1Result = device.create_bind_group_layout(group1Desc);
    assert(group1Result.ok());
    auto group1Layout = std::move(group1Result).value();
    group1Desc.entries.push_back({.binding = 8,
                                  .type = rhi::BindingType::storage_buffer,
                                  .visibility = rhi::ShaderStageMask::compute});
    assert(!device.create_bind_group_layout(group1Desc).ok());
    auto thirdGroup = device.create_bind_group_layout({.group = 2});
    assert(!thirdGroup.ok());

    auto pipelineLayoutResult = device.create_pipeline_layout(
        {.bindGroupLayouts = {&group0Layout, &group1Layout}});
    assert(pipelineLayoutResult.ok());
    auto pipelineLayout = std::move(pipelineLayoutResult).value();
    std::vector<rhi::ResourceBinding> reflection{
        {.name = "input",
         .stage = rhi::ShaderStage::compute,
         .group = 0,
         .binding = 0,
         .minimumSize = 16},
        {.name = "dynamicOutput",
         .stage = rhi::ShaderStage::compute,
         .group = 0,
         .binding = 1,
         .minimumSize = 4,
         .readOnly = false},
    };
    for (std::uint32_t binding = 0; binding < 8; ++binding) {
        reflection.push_back({.name = "out",
                              .stage = rhi::ShaderStage::compute,
                              .group = 1,
                              .binding = binding,
                              .minimumSize = 4,
                              .readOnly = false});
    }
    auto kernel = make_shader(device, rhi::ShaderStage::compute,
                              "groups_kernel", reflection);
    auto pipelineResult = device.create_compute_pipeline(
        {.computeShader = &kernel, .layout = &pipelineLayout});
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();

    auto uniform = make_buffer(device, 512, rhi::BufferUsage::uniform);
    const std::array<std::uint32_t, 4> input{100, 0xabcd, 0, 0};
    assert(uniform.write(256, std::as_bytes(std::span{input})).ok());
    auto dynamicStorage = make_buffer(device, 64, rhi::BufferUsage::storage);
    const std::array<std::uint32_t, 16> zeros{};
    assert(dynamicStorage.write(0, std::as_bytes(std::span{zeros})).ok());
    std::vector<rhi::Buffer> outputs;
    for (int index = 0; index < 8; ++index) {
        outputs.push_back(make_buffer(device, 16, rhi::BufferUsage::storage));
    }
    auto arenaResult = device.create_descriptor_arena();
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    auto group0 = device.create_bind_group({
        .layout = &group0Layout,
        .arena = &arena,
        .entries = {{.binding = 0, .buffer = &uniform, .size = 16},
                    {.binding = 1, .buffer = &dynamicStorage, .size = 16}},
    });
    rhi::BindGroupDesc group1Bindings{.layout = &group1Layout, .arena = &arena};
    for (std::uint32_t binding = 0; binding < 8; ++binding) {
        group1Bindings.entries.push_back(
            {.binding = binding, .buffer = &outputs[binding], .size = 16});
    }
    auto group1 = device.create_bind_group(group1Bindings);
    assert(group0.ok() && group1.ok());
    compute(device, [&](rhi::ComputeEncoder& encoder) {
        assert(encoder.bind_pipeline(pipeline).ok());
        assert(encoder
                   .bind_group(0, group0.value(),
                               std::array<std::uint32_t, 2>{256, 16})
                   .ok());
        assert(encoder.bind_group(1, group1.value()).ok());
        assert(encoder.dispatch(1, 1, 1).ok());
    });
    for (std::uint32_t index = 0; index < 8; ++index) {
        std::uint32_t value = 0;
        assert(outputs[index]
                   .read(0, std::as_writable_bytes(std::span{&value, 1}))
                   .ok());
        assert(value == 100 + index);
    }
    std::array<std::uint32_t, 16> dynamicWords{};
    assert(dynamicStorage
               .read(0, std::as_writable_bytes(std::span{dynamicWords}))
               .ok());
    assert(dynamicWords[0] == 0 && dynamicWords[4] == 0xabcd);
}

// Workgroups at the advertised invocation and depth limits.
void test_compute_limits(Fixture& fixture) {
    auto& device = fixture.device;
    for (const auto workgroup :
         {rhi::Extent3D{1024, 1, 1}, rhi::Extent3D{1, 1024, 1},
          rhi::Extent3D{1, 1, 64}, rhi::Extent3D{16, 1, 64}}) {
        const auto count = workgroup.width * workgroup.height * workgroup.depth;
        auto kernel = make_shader(device, rhi::ShaderStage::compute,
                                  "wide_kernel", {}, {}, workgroup);
        auto pipelineResult = device.create_compute_pipeline({.computeShader = &kernel});
        assert(pipelineResult.ok());
        auto pipeline = std::move(pipelineResult).value();
        auto output = make_buffer(device, count * 4, rhi::BufferUsage::storage);
        compute(device, [&](rhi::ComputeEncoder& encoder) {
            assert(encoder.bind_pipeline(pipeline).ok());
            assert(encoder.bind_storage_buffer(0, output).ok());
            assert(encoder.dispatch(1, 1, 1).ok());
        });
        std::vector<std::uint32_t> values(count);
        assert(output.read(0, std::as_writable_bytes(std::span{values})).ok());
        for (std::uint32_t index = 0; index < count; ++index) {
            assert(values[index] == index + 1);
        }
    }
}

// Explicit map/unmap on host-coherent upload and readback memory.
void test_mapping(Fixture& fixture) {
    auto& device = fixture.device;
    auto upload = make_buffer(device, 256, rhi::BufferUsage::copy_source);
    auto readback = make_buffer(device, 256, rhi::BufferUsage::copy_destination,
                                rhi::MemoryDomain::readback);
    auto mapped = upload.map();
    assert(mapped.ok() && mapped.value().size() == 256);
    for (std::size_t index = 0; index < 256; ++index) {
        mapped.value()[index] = std::byte{static_cast<unsigned char>(255 - index)};
    }
    assert(upload.flush().ok());
    assert(upload.unmap().ok());
    assert(copy(device, [&](rhi::CopyEncoder& encoder) {
               assert(encoder.copy_buffer(upload, readback, {.size = 256}).ok());
           }).ok());
    auto readMapped = readback.map();
    assert(readMapped.ok());
    assert(readback.invalidate().ok());
    for (std::size_t index = 0; index < 256; ++index) {
        assert(readMapped.value()[index] ==
               std::byte{static_cast<unsigned char>(255 - index)});
    }
    assert(readback.unmap().ok());
}

// A texture view that reinterprets rgba8_unorm storage as rgba8_srgb must
// decode on sample.
void test_view_reinterpretation(Fixture& fixture) {
    auto& device = fixture.device;
    auto texture = make_texture(device, {.extent = {16, 16, 1},
                                         .format = rhi::TextureFormat::rgba8_unorm,
                                         .usage = rhi::TextureUsage::sampled |
                                                  rhi::TextureUsage::copy_destination});
    std::vector<std::byte> texels(16 * 16 * 4, std::byte{128});
    upload_region(device, texture, whole(16, 16), texels, 4);
    auto viewResult = device.create_texture_view(
        texture, {.format = rhi::TextureFormat::rgba8_srgb});
    assert(viewResult.ok());
    auto view = std::move(viewResult).value();
    auto samplerResult = device.create_sampler({});
    assert(samplerResult.ok());
    auto sampler = std::move(samplerResult).value();
    auto layoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::sampled_texture,
                     .visibility = rhi::ShaderStageMask::fragment},
                    {.binding = 1,
                     .type = rhi::BindingType::sampler,
                     .visibility = rhi::ShaderStageMask::fragment}},
    });
    assert(layoutResult.ok());
    auto layout = std::move(layoutResult).value();
    auto pipelineLayoutResult =
        device.create_pipeline_layout({.bindGroupLayouts = {&layout}});
    assert(pipelineLayoutResult.ok());
    auto pipelineLayout = std::move(pipelineLayoutResult).value();
    auto fragmentShader = make_shader(
        device, rhi::ShaderStage::fragment, "sampled_fragment",
        {{.name = "source",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::texture,
          .binding = 0},
         {.name = "sourceSampler",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::sampler,
          .binding = 1}});
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &fixture.fullscreen,
        .fragmentShader = &fragmentShader,
        .layout = &pipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    auto arenaResult = device.create_descriptor_arena();
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    auto group = device.create_bind_group({
        .layout = &layout,
        .arena = &arena,
        .entries = {{.binding = 0, .textureView = &view},
                    {.binding = 1, .sampler = &sampler}},
    });
    assert(group.ok());
    auto target = make_texture(device, {.extent = {16, 16, 1},
                                        .format = rhi::TextureFormat::rgba8_unorm,
                                        .usage = rhi::TextureUsage::color_attachment |
                                                 rhi::TextureUsage::copy_source});
    render(device,
           {.extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
           [&](rhi::RenderEncoder& encoder) {
               assert(encoder.bind_pipeline(pipeline).ok());
               assert(encoder.bind_group(0, group.value()).ok());
               assert(encoder.draw(3).ok());
           });
    const auto pixels = read_region(device, target, whole(16, 16), 4);
    // sRGB 128/255 decodes to linear 0.2158 -> 55; alpha stays linear.
    assert(near_rgba8(std::span{pixels}.subspan(50 * 4, 4), 55, 55, 55, 128));
}

void test_unsupported_queries(rhi::Device& device) {
    for (const auto type : {rhi::QueryType::timestamp, rhi::QueryType::occlusion}) {
        const auto pool = device.create_query_pool({.type = type, .count = 2});
        assert(!pool.ok());
        assert(pool.status().code == StatusCode::unsupported);
    }
}

} // namespace

int main() {
    auto instanceResult = rhi::create_metal_instance();
    if (!instanceResult.ok()) {
        assert(instanceResult.status().code == StatusCode::unavailable);
        return 0;
    }
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    test_capabilities(adapter);
    auto deviceResult = adapter.request_device({});
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();

    const rhi::PushConstantRange colorRange{
        .stage = rhi::ShaderStage::fragment, .offset = 0, .size = 16};
    auto pushLayoutResult =
        device.create_pipeline_layout({.pushConstants = {colorRange}});
    assert(pushLayoutResult.ok());
    auto pushLayout = std::move(pushLayoutResult).value();
    auto fullscreen = make_shader(device, rhi::ShaderStage::vertex,
                                  "fullscreen_vertex");
    auto constantColor = make_shader(device, rhi::ShaderStage::fragment,
                                     "constant_fragment", {}, {colorRange});
    Fixture fixture{device, pushLayout, fullscreen, constantColor};

    test_color_formats(fixture);
    test_depth_formats(fixture);
    test_stencil(fixture);
    test_mip_levels(fixture);
    test_texture_transfers(fixture);
    test_vertex_buffers(fixture);
    test_viewports_and_scissors(fixture);
    test_eight_color_attachments(fixture);
    test_blend(fixture);
    test_push_constants(fixture);
    test_bind_groups(fixture);
    test_compute_limits(fixture);
    test_mapping(fixture);
    test_view_reinterpretation(fixture);
    test_unsupported_queries(device);
    assert(!device.lost());
    std::puts("Metal conformance: all capability checks passed");
    return 0;
}

#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> shader_bytes(std::string_view source) {
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    return {bytes.begin(), bytes.end()};
}

truffle::rhi::Shader make_shader(
    truffle::rhi::Device& device, truffle::rhi::ShaderStage stage,
    std::string_view source, std::string_view entryPoint,
    std::vector<truffle::rhi::ResourceBinding> reflection = {},
    std::vector<truffle::rhi::PushConstantRange> pushConstants = {},
    std::vector<truffle::rhi::ShaderSpecializationConstant> constants = {},
    std::vector<truffle::rhi::ShaderBindingMap> bindingMap = {},
    truffle::rhi::Extent3D requiredWorkgroup = {1, 1, 1},
    truffle::rhi::Extent3D preferredWorkgroup = {1, 1, 1}) {
    truffle::rhi::ShaderDesc desc;
    desc.stage = stage;
    desc.entryPoint = entryPoint;
    desc.code = shader_bytes(source);
    desc.reflection = std::move(reflection);
    desc.pushConstants = std::move(pushConstants);
    desc.specializationConstants = std::move(constants);
    desc.bindingMap = std::move(bindingMap);
    desc.requiredWorkgroupSize = requiredWorkgroup;
    desc.preferredWorkgroupSize = preferredWorkgroup;
    auto result = device.create_shader(desc);
    assert(result.ok());
    return std::move(result).value();
}

truffle::rhi::Texture make_render_texture(
    truffle::rhi::Device& device, truffle::rhi::TextureFormat format,
    std::uint32_t sampleCount = 1,
    truffle::rhi::TextureUsage extraUsage =
        truffle::rhi::TextureUsage::copy_source) {
    auto result = device.create_texture({
        .extent = {16, 16, 1},
        .format = format,
        .usage = truffle::rhi::TextureUsage::color_attachment | extraUsage,
        .sampleCount = sampleCount,
        .debugName = "Metal native render proof",
    });
    assert(result.ok());
    return std::move(result).value();
}

std::array<std::byte, 4> read_color_pixel(truffle::rhi::Device& device,
                                          truffle::rhi::Texture& texture,
                                          std::uint32_t x,
                                          std::uint32_t y) {
    constexpr std::size_t rowPitch = 256;
    auto readbackResult = device.create_buffer({
        .size = rowPitch * 16,
        .usage = truffle::rhi::BufferUsage::copy_destination,
        .memory = truffle::rhi::MemoryDomain::readback,
    });
    assert(readbackResult.ok());
    auto readback = std::move(readbackResult).value();
    auto poolResult =
        device.create_command_pool(truffle::rhi::QueueKind::transfer);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    auto copyResult = list.begin_copy();
    assert(copyResult.ok());
    auto copy = std::move(copyResult).value();
    assert(copy
               .copy_texture_to_buffer(
                   texture, readback,
                   {.layout = {.bytesPerRow = rowPitch, .rowsPerImage = 16},
                    .texture = {.extent = {16, 16, 1}}})
               .ok());
    assert(copy.end().ok());
    assert(list.end().ok());
    auto queueResult = device.queue(truffle::rhi::QueueKind::transfer);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<truffle::rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());
    std::array<std::byte, 4> pixel{};
    assert(readback.read(y * rowPitch + x * 4, pixel).ok());
    return pixel;
}

void submit_graphics(truffle::rhi::Device& device,
                     truffle::rhi::CommandList& list) {
    auto queueResult = device.queue(truffle::rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<truffle::rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());
}

} // namespace

int main() {
    using namespace truffle;
    auto instanceResult = rhi::create_metal_instance();
    if (!instanceResult.ok()) {
        assert(instanceResult.status().code == core::StatusCode::unavailable);
        return 0;
    }
    auto instance = std::move(instanceResult).value();
    assert(instance.backend() == rhi::BackendKind::metal);
    assert(instance.adapter_count() == 1);

    auto adapterResult = instance.adapter(0);
    assert(adapterResult.ok());
    auto adapter = std::move(adapterResult).value();
    assert(adapter.info().native);
    assert(!adapter.info().validationOnly);
    assert(adapter.info().presentation);
    assert(adapter.info().resources.bufferCopy);
    assert(adapter.info().resources.bufferTextureCopy);
    assert(adapter.info().resources.textureCopy);
    assert(!adapter.info().resources.textureClear);
    assert(!adapter.info().resources.textureResolve);
    assert(!adapter.info().resources.externalImport);
    assert(adapter.info().bindings.ordinaryBindGroups);
    assert(adapter.info().bindings.descriptorArrays);
    assert(adapter.info().bindings.dynamicOffsets);
    assert(adapter.info().bindings.immutableSamplers);
    assert(adapter.info().bindings.pushConstants);
    assert(!adapter.info().bindings.bindlessTables);
    assert(adapter.info().pipelines.graphics);
    assert(adapter.info().pipelines.compute);
    assert(adapter.info().pipelines.multipleRenderTargets);
    assert(adapter.info().pipelines.depthStencil);
    assert(adapter.info().pipelines.multisample);
    assert(adapter.info().pipelines.indirect);
    assert(!adapter.info().pipelines.indirectCount);
    assert(!adapter.info().pipelines.pipelineCache);

    auto deviceResult = adapter.request_device({
        .requiredFeatures = {rhi::Feature::transfer,
                             rhi::Feature::memory_budget},
    });
    assert(deviceResult.ok());
    auto device = std::move(deviceResult).value();
    assert(!device.lost());
    assert(device.memory_budget(rhi::MemoryDomain::device_local).ok());

    auto computeQueueProof = device.queue(rhi::QueueKind::compute);
    assert(computeQueueProof.ok());
    auto nativeFenceResult = device.create_fence();
    auto nativeSemaphoreResult = device.create_semaphore();
    assert(nativeFenceResult.ok() && nativeSemaphoreResult.ok());
    auto nativeFence = std::move(nativeFenceResult).value();
    auto nativeSemaphore = std::move(nativeSemaphoreResult).value();

    constexpr std::size_t byteCount = 64;
    auto uploadResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .mappedAtCreation = true,
        .debugName = "Metal upload proof",
    });
    auto readbackResult = device.create_buffer({
        .size = byteCount,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
        .debugName = "Metal readback proof",
    });
    assert(uploadResult.ok() && readbackResult.ok());
    auto upload = std::move(uploadResult).value();
    auto readback = std::move(readbackResult).value();
    auto uploadView = device.create_buffer_view(upload);
    assert(uploadView.ok() && uploadView.value().desc().size == byteCount);

    std::array<std::byte, byteCount> sourceBytes{};
    for (std::size_t index = 0; index < sourceBytes.size(); ++index) {
        sourceBytes[index] = std::byte{static_cast<unsigned char>(index ^ 0x5a)};
    }
    assert(upload.write(0, sourceBytes).ok());

    constexpr std::uint32_t width = 4;
    constexpr std::uint32_t height = 4;
    const rhi::TextureRegion wholeTexture{
        .extent = {width, height, 1},
    };
    auto sourceTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
        .debugName = "Metal texture upload proof",
    });
    auto destinationTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
        .debugName = "Metal texture readback proof",
    });
    assert(sourceTextureResult.ok() && destinationTextureResult.ok());
    auto sourceTexture = std::move(sourceTextureResult).value();
    auto destinationTexture = std::move(destinationTextureResult).value();
    assert(sourceTexture.write(wholeTexture, sourceBytes).ok());
    auto textureView = device.create_texture_view(
        sourceTexture, {.format = rhi::TextureFormat::rgba8_srgb});
    assert(textureView.ok());
    assert(textureView.value().desc().format ==
           rhi::TextureFormat::rgba8_srgb);

    constexpr std::size_t metalRowPitch = 256;
    constexpr std::size_t paddedTextureBytes = metalRowPitch * height;
    auto paddedUploadResult = device.create_buffer({
        .size = paddedTextureBytes,
        .usage = rhi::BufferUsage::copy_source,
        .memory = rhi::MemoryDomain::upload,
    });
    auto paddedReadbackResult = device.create_buffer({
        .size = paddedTextureBytes,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    auto deviceTextureResult = device.create_texture({
        .extent = {width, height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::copy_source |
                 rhi::TextureUsage::copy_destination,
    });
    assert(paddedUploadResult.ok() && paddedReadbackResult.ok() &&
           deviceTextureResult.ok());
    auto paddedUpload = std::move(paddedUploadResult).value();
    auto paddedReadback = std::move(paddedReadbackResult).value();
    auto deviceTexture = std::move(deviceTextureResult).value();
    std::array<std::byte, paddedTextureBytes> paddedPixels{};
    for (std::size_t row = 0; row < height; ++row) {
        std::copy_n(sourceBytes.begin() +
                        static_cast<std::ptrdiff_t>(row * width * 4),
                    static_cast<std::ptrdiff_t>(width * 4),
                    paddedPixels.begin() +
                        static_cast<std::ptrdiff_t>(row * metalRowPitch));
    }
    assert(paddedUpload.write(0, paddedPixels).ok());

    auto poolResult = device.create_command_pool(rhi::QueueKind::transfer);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    auto encoderResult = list.begin_copy();
    assert(encoderResult.ok());
    auto encoder = std::move(encoderResult).value();
    assert(encoder
               .copy_buffer(upload, readback,
                            {.sourceOffset = 0,
                             .destinationOffset = 0,
                             .size = byteCount})
               .ok());
    const rhi::BufferTextureCopyRegion paddedRegion{
        .layout = {.bytesPerRow = metalRowPitch, .rowsPerImage = height},
        .texture = wholeTexture,
    };
    assert(encoder
               .copy_buffer_to_texture(paddedUpload, deviceTexture, paddedRegion)
               .ok());
    assert(encoder
               .copy_texture_to_buffer(deviceTexture, paddedReadback,
                                       paddedRegion)
               .ok());
    assert(encoder
               .copy_texture(sourceTexture, destinationTexture,
                             {.source = wholeTexture,
                              .destination = wholeTexture})
               .ok());
    assert(encoder.fill_buffer(paddedReadback, 64, 16, std::byte{0x7f}).ok());
    assert(encoder.end().ok());
    assert(list.end().ok());

    auto queueResult = device.queue(rhi::QueueKind::transfer);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> lists{&list};
    assert(queue.submit(lists).ok());

    std::array<std::byte, byteCount> bufferOutput{};
    std::array<std::byte, byteCount> textureOutput{};
    assert(readback.read(0, bufferOutput).ok());
    assert(destinationTexture.read(wholeTexture, textureOutput).ok());
    assert(bufferOutput == sourceBytes);
    assert(textureOutput == sourceBytes);
    std::array<std::byte, paddedTextureBytes> paddedOutput{};
    assert(paddedReadback.read(0, paddedOutput).ok());
    std::fill_n(paddedPixels.begin() + 64, 16, std::byte{0x7f});
    assert(paddedOutput == paddedPixels);

    auto unsupportedTexture = device.create_texture({
        .dimension = rhi::TextureDimension::d3,
        .extent = {4, 4, 4},
        .usage = rhi::TextureUsage::copy_source,
    });
    assert(!unsupportedTexture.ok());
    assert(unsupportedTexture.status().code == core::StatusCode::unsupported);

    constexpr std::string_view msl = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexOutput {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOutput triangle_vertex(uint vertexId [[vertex_id]],
                                    uint instanceId [[instance_id]]) {
    const float2 positions[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    const float2 coordinates[3] = {
        float2(0.0, 0.0), float2(2.0, 0.0), float2(0.0, 2.0)
    };
    const uint index = vertexId % 3;
    VertexOutput output;
    output.position = float4(positions[index], 0.5, 1.0);
    output.position.x += float(instanceId % 2) * 0.0;
    output.uv = coordinates[index];
    return output;
}

fragment float4 red_fragment(VertexOutput input [[stage_in]]) {
    (void)input;
    return float4(1.0, 0.0, 0.0, 1.0);
}

constant bool useSecondTexture [[function_constant(7)]];

fragment float4 textured_fragment(
    VertexOutput input [[stage_in]],
    texture2d<float> firstTexture [[texture(3)]],
    texture2d<float> secondTexture [[texture(5)]],
    sampler textureSampler [[sampler(2)]],
    constant float4& tint [[buffer(7)]],
    constant float4& pushed [[buffer(30)]]) {
    const float4 sampled = useSecondTexture
        ? secondTexture.sample(textureSampler, input.uv)
        : firstTexture.sample(textureSampler, input.uv);
    return sampled * tint * pushed;
}

struct MultipleOutputs {
    float4 first [[color(0)]];
    float4 second [[color(1)]];
};

fragment MultipleOutputs mrt_fragment(VertexOutput input [[stage_in]]) {
    (void)input;
    MultipleOutputs output;
    output.first = float4(1.0, 0.0, 0.0, 1.0);
    output.second = float4(0.0, 1.0, 0.0, 1.0);
    return output;
}

kernel void compute_color(device float4* color [[buffer(4)]],
                          uint threadId [[thread_position_in_grid]]) {
    if (threadId == 0) {
        color[0] = float4(0.0, 0.0, 1.0, 1.0);
    }
}

fragment float4 buffer_fragment(VertexOutput input [[stage_in]],
                                device const float4* color [[buffer(6)]]) {
    (void)input;
    return color[0];
}
)MSL";

    rhi::ShaderPackageDesc nativePackageDesc;
    nativePackageDesc.name = "Metal native pipeline proof";
    nativePackageDesc.sources = {
        {.path = "native-pipeline-proof.metal",
         .language = rhi::ShaderSourceLanguage::msl,
         .sha256 =
             "2a8a18719f0108b79f2cac51a04a424189112ca2a2d8d56e489cbca0022358f4"},
    };
    nativePackageDesc.compilers = {
        {.name = "Metal runtime compiler", .version = "host SDK"},
    };
    nativePackageDesc.variants = {
        {.target = rhi::ShaderTarget::metal_source,
         .format = rhi::ShaderByteFormat::native_source,
         .kind = rhi::ShaderVariantKind::native_override,
         .stage = rhi::ShaderStage::vertex,
         .entryPoint = "triangle_vertex",
         .code = shader_bytes(msl)},
        {.target = rhi::ShaderTarget::metal_source,
         .format = rhi::ShaderByteFormat::native_source,
         .kind = rhi::ShaderVariantKind::native_override,
         .stage = rhi::ShaderStage::fragment,
         .entryPoint = "red_fragment",
         .code = shader_bytes(msl)},
    };
    auto nativePackageResult =
        rhi::ShaderPackage::create(std::move(nativePackageDesc));
    assert(nativePackageResult.ok());
    auto nativePackage = std::move(nativePackageResult).value();
    auto triangleVertexResult = device.create_shader(
        nativePackage, rhi::ShaderTarget::metal_source, "triangle_vertex",
        rhi::ShaderStage::vertex);
    auto redFragmentResult = device.create_shader(
        nativePackage, rhi::ShaderTarget::metal_source, "red_fragment",
        rhi::ShaderStage::fragment);
    assert(triangleVertexResult.ok() && redFragmentResult.ok());
    auto triangleVertex = std::move(triangleVertexResult).value();
    auto redFragment = std::move(redFragmentResult).value();
    auto trianglePipelineResult = device.create_pipeline({
        .vertexShader = &triangleVertex,
        .fragmentShader = &redFragment,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(trianglePipelineResult.ok());
    auto trianglePipeline = std::move(trianglePipelineResult).value();
    auto triangleTarget = make_render_texture(
        device, rhi::TextureFormat::rgba8_unorm);
    auto graphicsPoolResult =
        device.create_command_pool(rhi::QueueKind::graphics);
    assert(graphicsPoolResult.ok());
    auto graphicsPool = std::move(graphicsPoolResult).value();
    auto triangleListResult = graphicsPool.allocate();
    assert(triangleListResult.ok());
    auto triangleList = std::move(triangleListResult).value();
    assert(triangleList.begin().ok());
    auto triangleEncoderResult = triangleList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &triangleTarget,
                              .clear = {0.0F, 0.0F, 0.0F, 1.0F}}},
    });
    assert(triangleEncoderResult.ok());
    auto triangleEncoder = std::move(triangleEncoderResult).value();
    assert(triangleEncoder.bind_pipeline(trianglePipeline).ok());
    assert(triangleEncoder.draw(3, 1, 3, 2).ok());
    assert(triangleEncoder.end().ok());
    assert(triangleList.end().ok());
    submit_graphics(device, triangleList);
    const auto redPixel = read_color_pixel(device, triangleTarget, 8, 8);
    const std::array<std::byte, 4> expectedRed{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    assert(redPixel == expectedRed);

    // A render pass followed by a copy in the same command list must preserve
    // that exact order. The old split transfer/command arrays copied first.
    auto orderedReadbackResult = device.create_buffer({
        .size = 256 * 16,
        .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    });
    assert(orderedReadbackResult.ok());
    auto orderedReadback = std::move(orderedReadbackResult).value();
    auto orderedListResult = graphicsPool.allocate();
    assert(orderedListResult.ok());
    auto orderedList = std::move(orderedListResult).value();
    assert(orderedList.begin().ok());
    rhi::BarrierBatch renderBarrier;
    renderBarrier.textures.push_back({
        .texture = &triangleTarget,
        .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::color_attachment,
        .destinationStages = rhi::PipelineStage::color_attachment_output,
        .destinationAccess = rhi::Access::color_attachment_write,
    });
    assert(orderedList.barrier(renderBarrier).ok());
    auto clearResult = orderedList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &triangleTarget,
                              .clear = {0.0F, 1.0F, 0.0F, 1.0F}}},
    });
    assert(clearResult.ok());
    auto clear = std::move(clearResult).value();
    assert(clear.end().ok());
    rhi::BarrierBatch copyBarrier;
    copyBarrier.textures.push_back({
        .texture = &triangleTarget,
        .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::transfer_source,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = rhi::Access::color_attachment_write,
        .destinationAccess = rhi::Access::transfer_read,
    });
    assert(orderedList.barrier(copyBarrier).ok());
    auto orderedCopyResult = orderedList.begin_copy();
    assert(orderedCopyResult.ok());
    auto orderedCopy = std::move(orderedCopyResult).value();
    assert(orderedCopy
               .copy_texture_to_buffer(
                   triangleTarget, orderedReadback,
                   {.layout = {.bytesPerRow = 256, .rowsPerImage = 16},
                    .texture = {.extent = {16, 16, 1}}})
               .ok());
    assert(orderedCopy.end().ok());
    assert(orderedList.end().ok());
    auto graphicsQueueResult = device.queue(rhi::QueueKind::graphics);
    assert(graphicsQueueResult.ok());
    auto graphicsQueue = std::move(graphicsQueueResult).value();
    std::array<rhi::CommandList*, 1> orderedLists{&orderedList};
    const std::array<rhi::SemaphoreSignal, 1> orderedSignals{{
        {.semaphore = &nativeSemaphore, .value = 3},
    }};
    assert(graphicsQueue
               .submit({.commandLists = orderedLists,
                        .signals = orderedSignals,
                        .signalFence = &nativeFence,
                        .signalFenceValue = 3})
               .ok());
    assert(nativeSemaphore.value() == 3);
    assert(nativeFence.completed_value() == 3);
    std::array<std::byte, 4> orderedPixel{};
    assert(orderedReadback.read(8 * 256 + 8 * 4, orderedPixel).ok());
    const std::array<std::byte, 4> expectedGreen{
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    assert(orderedPixel == expectedGreen);
    auto crossQueueListResult = pool.allocate();
    assert(crossQueueListResult.ok());
    auto crossQueueList = std::move(crossQueueListResult).value();
    assert(crossQueueList.begin().ok());
    assert(crossQueueList.end().ok());
    std::array<rhi::CommandList*, 1> crossQueueLists{&crossQueueList};
    const std::array<rhi::SemaphoreWait, 1> crossQueueWaits{{
        {.semaphore = &nativeSemaphore,
         .value = 3,
         .stages = rhi::PipelineStage::copy},
    }};
    const std::array<rhi::SemaphoreSignal, 1> crossQueueSignals{{
        {.semaphore = &nativeSemaphore, .value = 4},
    }};
    assert(queue
               .submit({.commandLists = crossQueueLists,
                        .waits = crossQueueWaits,
                        .signals = crossQueueSignals})
               .ok());
    assert(nativeSemaphore.value() == 4);

    auto texture0 = device.create_texture({
        .extent = {1, 1, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled,
        .memory = rhi::MemoryDomain::upload,
    });
    auto texture1 = device.create_texture({
        .extent = {1, 1, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(texture0.ok() && texture1.ok());
    auto redTexture = std::move(texture0).value();
    auto greenTexture = std::move(texture1).value();
    const std::array<std::byte, 4> redTexel{
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    const std::array<std::byte, 4> greenTexel{
        std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    assert(redTexture.write({.extent = {1, 1, 1}}, redTexel).ok());
    assert(greenTexture.write({.extent = {1, 1, 1}}, greenTexel).ok());
    auto redViewResult = device.create_texture_view(redTexture);
    auto greenViewResult = device.create_texture_view(greenTexture);
    assert(redViewResult.ok() && greenViewResult.ok());
    auto redView = std::move(redViewResult).value();
    auto greenView = std::move(greenViewResult).value();
    auto nativeSamplerResult = device.create_sampler({
        .minFilter = rhi::Filter::nearest,
        .magFilter = rhi::Filter::nearest,
    });
    assert(nativeSamplerResult.ok());
    auto nativeSampler = std::move(nativeSamplerResult).value();
    auto texturedLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {
            {.binding = 0,
             .type = rhi::BindingType::sampled_texture,
             .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 1,
             .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment,
             .immutableSampler = &nativeSampler},
            {.binding = 2,
             .type = rhi::BindingType::uniform_buffer,
             .visibility = rhi::ShaderStageMask::fragment,
             .dynamicOffset = true,
             .minimumBufferSize = 16},
        },
    });
    assert(texturedLayoutResult.ok());
    auto texturedLayout = std::move(texturedLayoutResult).value();
    auto texturedPipelineLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&texturedLayout},
        .pushConstants = {
            {.stage = rhi::ShaderStage::fragment, .offset = 0, .size = 16},
        },
    });
    assert(texturedPipelineLayoutResult.ok());
    auto texturedPipelineLayout =
        std::move(texturedPipelineLayoutResult).value();
    auto tintBufferResult = device.create_buffer({
        .size = 512,
        .usage = rhi::BufferUsage::uniform,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(tintBufferResult.ok());
    auto tintBuffer = std::move(tintBufferResult).value();
    const std::array<float, 4> white{1.0F, 1.0F, 1.0F, 1.0F};
    assert(tintBuffer.write(256, std::as_bytes(std::span{white})).ok());
    auto descriptorArenaResult = device.create_descriptor_arena();
    assert(descriptorArenaResult.ok());
    auto descriptorArena = std::move(descriptorArenaResult).value();
    auto texturedGroupResult = device.create_bind_group({
        .layout = &texturedLayout,
        .arena = &descriptorArena,
        .entries = {{.binding = 0,
                     .arrayElement = 0,
                     .textureView = &redView},
                    {.binding = 0,
                     .arrayElement = 1,
                     .textureView = &greenView},
                    {.binding = 2, .buffer = &tintBuffer, .size = 16}},
    });
    assert(texturedGroupResult.ok());
    auto texturedGroup = std::move(texturedGroupResult).value();
    auto texturedFragment = make_shader(
        device, rhi::ShaderStage::fragment, msl, "textured_fragment",
        {{.name = "textures",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::texture,
          .group = 0,
          .binding = 0,
          .arrayCount = 2},
         {.name = "textureSampler",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::sampler,
          .group = 0,
          .binding = 1},
         {.name = "tint",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::buffer,
          .group = 0,
          .binding = 2,
          .minimumSize = 16}},
        {{.stage = rhi::ShaderStage::fragment, .offset = 0, .size = 16}},
        {{.id = 7,
          .name = "useSecondTexture",
          .type = rhi::ShaderValueType::boolean,
          .defaultValueBits = 0}},
        {{.stage = rhi::ShaderStage::fragment,
          .group = 0,
          .binding = 0,
          .arrayElement = 0,
          .nativeBinding = 3},
         {.stage = rhi::ShaderStage::fragment,
          .group = 0,
          .binding = 0,
          .arrayElement = 1,
          .nativeBinding = 5},
         {.stage = rhi::ShaderStage::fragment,
          .group = 0,
          .binding = 1,
          .nativeBinding = 2},
         {.stage = rhi::ShaderStage::fragment,
          .group = 0,
          .binding = 2,
          .nativeBinding = 7}});
    auto texturedPipelineResult = device.create_pipeline({
        .vertexShader = &triangleVertex,
        .fragmentShader = &texturedFragment,
        .layout = &texturedPipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
        .specializationConstants = {
            {.id = 7, .type = rhi::ShaderValueType::boolean, .valueBits = 1},
        },
    });
    assert(texturedPipelineResult.ok());
    auto texturedPipeline = std::move(texturedPipelineResult).value();
    auto texturedTarget = make_render_texture(
        device, rhi::TextureFormat::rgba8_unorm);
    auto texturedListResult = graphicsPool.allocate();
    assert(texturedListResult.ok());
    auto texturedList = std::move(texturedListResult).value();
    assert(texturedList.begin().ok());
    auto texturedEncoderResult = texturedList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &texturedTarget}},
    });
    assert(texturedEncoderResult.ok());
    auto texturedEncoder = std::move(texturedEncoderResult).value();
    assert(texturedEncoder.bind_pipeline(texturedPipeline).ok());
    assert(texturedEncoder
               .bind_group(0, texturedGroup,
                           std::array<std::uint32_t, 1>{256})
               .ok());
    assert(texturedEncoder
               .push_constants(rhi::ShaderStageMask::fragment, 0,
                               std::as_bytes(std::span{white}))
               .ok());
    assert(texturedEncoder.draw(3).ok());
    assert(texturedEncoder.end().ok());
    assert(texturedList.end().ok());
    submit_graphics(device, texturedList);
    const auto greenPixel = read_color_pixel(device, texturedTarget, 8, 8);
    assert(greenPixel == greenTexel);

    auto mrtFragment = make_shader(device, rhi::ShaderStage::fragment, msl,
                                   "mrt_fragment");
    auto mrtPipelineResult = device.create_pipeline({
        .vertexShader = &triangleVertex,
        .fragmentShader = &mrtFragment,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm},
                         {.format = rhi::TextureFormat::rgba8_unorm}},
        .depthStencil = {.format = rhi::TextureFormat::depth32_float,
                         .depthWriteEnabled = true,
                         .depthCompare = rhi::CompareOp::less},
        .multisample = {.sampleCount = 4},
    });
    assert(mrtPipelineResult.ok());
    auto mrtPipeline = std::move(mrtPipelineResult).value();
    auto multisample0 = make_render_texture(
        device, rhi::TextureFormat::rgba8_unorm, 4,
        rhi::TextureUsage::none);
    auto multisample1 = make_render_texture(
        device, rhi::TextureFormat::rgba8_unorm, 4,
        rhi::TextureUsage::none);
    auto resolve0 = make_render_texture(device,
                                        rhi::TextureFormat::rgba8_unorm);
    auto resolve1 = make_render_texture(device,
                                        rhi::TextureFormat::rgba8_unorm);
    auto depthResult = device.create_texture({
        .extent = {16, 16, 1},
        .format = rhi::TextureFormat::depth32_float,
        .usage = rhi::TextureUsage::depth_stencil_attachment,
        .sampleCount = 4,
    });
    assert(depthResult.ok());
    auto depth = std::move(depthResult).value();
    auto indexResult = device.create_buffer({
        .size = 3 * sizeof(std::uint16_t),
        .usage = rhi::BufferUsage::index,
        .memory = rhi::MemoryDomain::upload,
    });
    auto indirectResult = device.create_buffer({
        .size = 4 * sizeof(std::uint32_t),
        .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(indexResult.ok() && indirectResult.ok());
    auto indices = std::move(indexResult).value();
    auto indirectDraw = std::move(indirectResult).value();
    const std::array<std::uint16_t, 3> indexData{0, 1, 2};
    const std::array<std::uint32_t, 4> indirectData{3, 1, 0, 0};
    assert(indices.write(0, std::as_bytes(std::span{indexData})).ok());
    assert(indirectDraw.write(0, std::as_bytes(std::span{indirectData})).ok());
    auto mrtListResult = graphicsPool.allocate();
    assert(mrtListResult.ok());
    auto mrtList = std::move(mrtListResult).value();
    assert(mrtList.begin().ok());
    auto mrtEncoderResult = mrtList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &multisample0,
                              .resolveTexture = &resolve0},
                             {.texture = &multisample1,
                              .resolveTexture = &resolve1}},
        .depthStencilAttachment = {.texture = &depth},
    });
    assert(mrtEncoderResult.ok());
    auto mrtEncoder = std::move(mrtEncoderResult).value();
    assert(mrtEncoder.bind_pipeline(mrtPipeline).ok());
    assert(mrtEncoder.bind_index_buffer(indices, 0, rhi::IndexFormat::uint16)
               .ok());
    assert(mrtEncoder.draw_indexed(3, 2, 0, 0, 0).ok());
    assert(mrtEncoder.draw_indirect(indirectDraw, 0, false).ok());
    assert(mrtEncoder.end().ok());
    assert(mrtList.end().ok());
    submit_graphics(device, mrtList);
    assert(read_color_pixel(device, resolve0, 8, 8) == redTexel);
    assert(read_color_pixel(device, resolve1, 8, 8) == greenTexel);

    auto sharedLayoutResult = device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute |
                                   rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = 16}},
    });
    assert(sharedLayoutResult.ok());
    auto sharedLayout = std::move(sharedLayoutResult).value();
    auto sharedPipelineLayoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&sharedLayout},
    });
    assert(sharedPipelineLayoutResult.ok());
    auto sharedPipelineLayout =
        std::move(sharedPipelineLayoutResult).value();
    auto colorBufferResult = device.create_buffer({
        .size = 16,
        .usage = rhi::BufferUsage::storage,
        .memory = rhi::MemoryDomain::upload,
    });
    assert(colorBufferResult.ok());
    auto colorBuffer = std::move(colorBufferResult).value();
    auto sharedGroupResult = device.create_bind_group({
        .layout = &sharedLayout,
        .arena = &descriptorArena,
        .entries = {{.binding = 0, .buffer = &colorBuffer, .size = 16}},
    });
    assert(sharedGroupResult.ok());
    auto sharedGroup = std::move(sharedGroupResult).value();
    auto computeShader = make_shader(
        device, rhi::ShaderStage::compute, msl, "compute_color",
        {{.name = "color",
          .stage = rhi::ShaderStage::compute,
          .type = rhi::ResourceBindingType::buffer,
          .group = 0,
          .binding = 0,
          .minimumSize = 16,
          .readOnly = false}},
        {}, {}, {{.stage = rhi::ShaderStage::compute,
                  .group = 0,
                  .binding = 0,
                  .nativeBinding = 4}},
        {1, 1, 1}, {8, 1, 1});
    auto computePipelineResult = device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &sharedPipelineLayout,
    });
    assert(computePipelineResult.ok());
    auto computePipeline = std::move(computePipelineResult).value();
    assert(computePipeline.preferred_workgroup_size() ==
           rhi::Extent3D(8, 1, 1));
    auto computePoolResult = device.create_command_pool(rhi::QueueKind::compute);
    assert(computePoolResult.ok());
    auto computePool = std::move(computePoolResult).value();
    auto computeListResult = computePool.allocate();
    assert(computeListResult.ok());
    auto computeList = std::move(computeListResult).value();
    assert(computeList.begin().ok());
    auto computeEncoderResult = computeList.begin_compute();
    assert(computeEncoderResult.ok());
    auto computeEncoder = std::move(computeEncoderResult).value();
    assert(computeEncoder.bind_pipeline(computePipeline).ok());
    assert(computeEncoder.bind_group(0, sharedGroup).ok());
    assert(computeEncoder.dispatch(1, 1, 1).ok());
    assert(computeEncoder.end().ok());
    assert(computeList.end().ok());
    auto nativeComputeQueueResult = device.queue(rhi::QueueKind::compute);
    assert(nativeComputeQueueResult.ok());
    auto nativeComputeQueue = std::move(nativeComputeQueueResult).value();
    std::array<rhi::CommandList*, 1> computeLists{&computeList};
    assert(nativeComputeQueue.submit(computeLists).ok());

    auto bufferFragment = make_shader(
        device, rhi::ShaderStage::fragment, msl, "buffer_fragment",
        {{.name = "color",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::buffer,
          .group = 0,
          .binding = 0,
          .minimumSize = 16,
          .readOnly = true}},
        {}, {}, {{.stage = rhi::ShaderStage::fragment,
                  .group = 0,
                  .binding = 0,
                  .nativeBinding = 6}});
    auto bufferPipelineResult = device.create_pipeline({
        .vertexShader = &triangleVertex,
        .fragmentShader = &bufferFragment,
        .layout = &sharedPipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
    });
    assert(bufferPipelineResult.ok());
    auto bufferPipeline = std::move(bufferPipelineResult).value();
    auto computeRenderTarget = make_render_texture(
        device, rhi::TextureFormat::rgba8_unorm);
    auto bufferListResult = graphicsPool.allocate();
    assert(bufferListResult.ok());
    auto bufferList = std::move(bufferListResult).value();
    assert(bufferList.begin().ok());
    auto bufferEncoderResult = bufferList.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &computeRenderTarget}},
    });
    assert(bufferEncoderResult.ok());
    auto bufferEncoder = std::move(bufferEncoderResult).value();
    assert(bufferEncoder.bind_pipeline(bufferPipeline).ok());
    assert(bufferEncoder.bind_group(0, sharedGroup).ok());
    assert(bufferEncoder.draw(3).ok());
    assert(bufferEncoder.end().ok());
    assert(bufferList.end().ok());
    submit_graphics(device, bufferList);
    const std::array<std::byte, 4> blueTexel{
        std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    assert(read_color_pixel(device, computeRenderTarget, 8, 8) == blueTexel);

    const auto unsupportedCache = device.create_pipeline_cache();
    assert(!unsupportedCache.ok());
    assert(unsupportedCache.status().code == core::StatusCode::unsupported);
    const auto unsupportedBindless = device.create_bindless_table({
        .layout = &texturedLayout,
        .capacity = 64,
    });
    assert(!unsupportedBindless.ok());
    assert(unsupportedBindless.status().code == core::StatusCode::unsupported);

    const auto stats = instance.stats();
    assert(stats.buffersCreated >= 12);
    assert(stats.texturesCreated >= 13);
    assert(stats.transfersExecuted >= 10);
    assert(stats.submissions >= 10);
    assert(stats.drawsRecorded == 5);
    assert(stats.dispatchesRecorded == 1);
    return 0;
}

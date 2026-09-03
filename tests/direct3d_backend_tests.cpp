#include "truffle/rhi/direct3d_backend.hpp"
#include "truffle/rhi/shader_package.hpp"

#include "native_backend_smoke.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rhi = truffle::rhi;

std::vector<std::byte> bytes(std::string_view source) {
  const auto raw = std::as_bytes(std::span{source.data(), source.size()});
  return {raw.begin(), raw.end()};
}

rhi::Shader make_hlsl_shader(rhi::Device &device, std::string name,
                             rhi::ShaderStage stage, std::string entryPoint,
                             std::string_view source,
                             std::vector<rhi::ResourceBinding> reflection = {},
                             std::vector<rhi::PushConstantRange> pushConstants = {},
                             std::vector<rhi::ShaderBindingMap> bindingMap = {}) {
  rhi::ShaderPackageDesc packageDesc;
  packageDesc.name = std::move(name);
  packageDesc.sources.push_back({.path = "native-test.hlsl",
                                 .language = rhi::ShaderSourceLanguage::hlsl,
                                 .sha256 = std::string(64, '0')});
  packageDesc.compilers.push_back(
      {.name = "Windows SDK D3DCompiler", .version = "system"});
  packageDesc.variants.push_back({
      .target = rhi::ShaderTarget::dxil,
      .format = rhi::ShaderByteFormat::native_source,
      .kind = rhi::ShaderVariantKind::native_override,
      .stage = stage,
      .entryPoint = entryPoint,
      .reflection = {.bindings = reflection, .pushConstants = pushConstants},
      .code = bytes(source),
  });
  for (const auto &mapping : bindingMap) {
    packageDesc.remaps.push_back({
        .target = rhi::ShaderTarget::dxil,
        .stage = mapping.stage,
        .group = mapping.group,
        .binding = mapping.binding,
        .arrayElement = mapping.arrayElement,
        .nativeGroup = mapping.nativeGroup,
        .nativeBinding = mapping.nativeBinding,
        .nativeArrayElement = mapping.nativeArrayElement,
    });
  }
  auto package = rhi::ShaderPackage::create(std::move(packageDesc));
  assert(package.ok());
  auto shader =
      device.create_shader(package.value(), rhi::ShaderTarget::dxil, entryPoint, stage);
  assert(shader.ok());
  return std::move(shader).value();
}

void submit(rhi::Device &device, rhi::Queue &queue,
            const std::function<void(rhi::CommandList &)> &record) {
  auto pool = device.create_command_pool(rhi::QueueKind::graphics);
  assert(pool.ok());
  auto list = pool.value().allocate();
  assert(list.ok());
  assert(list.value().begin().ok());
  record(list.value());
  assert(list.value().end().ok());
  std::array<rhi::CommandList *, 1> lists{&list.value()};
  assert(queue.submit(lists).ok());
}

std::vector<std::byte> read_rgba8(rhi::Device &device, rhi::Queue &queue,
                                  rhi::Texture &texture, std::uint32_t width,
                                  std::uint32_t height) {
  constexpr std::size_t rowPitch = 256;
  auto readback = device.create_buffer({
      .size = rowPitch * height,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  });
  assert(readback.ok());
  submit(device, queue, [&](rhi::CommandList &list) {
    auto copy = list.begin_copy();
    assert(copy.ok());
    assert(copy.value()
               .fill_buffer(readback.value(), 0, rowPitch * height, std::byte{})
               .ok());
    assert(copy.value()
               .copy_texture_to_buffer(
                   texture, readback.value(),
                   {.layout = {.bytesPerRow = rowPitch, .rowsPerImage = height},
                    .texture = {.subresource = {.aspect = rhi::TextureAspect::color},
                                .extent = {width, height, 1}}})
               .ok());
    assert(copy.value().end().ok());
  });
  std::vector<std::byte> output(rowPitch * height);
  assert(readback.value().read(0, output).ok());
  return output;
}

void assert_solid_rgba8(std::span<const std::byte> pixels, std::uint32_t width,
                        std::uint32_t height, std::array<std::byte, 4> expected) {
  constexpr std::size_t rowPitch = 256;
  for (std::uint32_t row = 0; row < height; ++row) {
    for (std::uint32_t column = 0; column < width; ++column) {
      const auto offset = row * rowPitch + column * 4u;
      assert(std::equal(expected.begin(), expected.end(), pixels.begin() + offset));
    }
    assert(std::all_of(pixels.begin() + row * rowPitch + width * 4u,
                       pixels.begin() + (row + 1u) * rowPitch,
                       [](std::byte value) { return value == std::byte{}; }));
  }
}

void verify_direct3d_buffers() {
  auto instanceResult = rhi::create_direct3d12_instance();
  assert(instanceResult.ok());
  auto instance = std::move(instanceResult).value();
  auto adapterResult = instance.adapter(0);
  assert(adapterResult.ok());
  auto adapter = std::move(adapterResult).value();
  const auto info = adapter.info();
  assert(info.backend == rhi::BackendKind::direct3d12);
  assert(info.platform == rhi::PlatformKind::windows);
  assert(info.maturity == rhi::BackendMaturity::native_smoke);
  assert(info.native && !info.validationOnly);
  assert(info.resources.bufferViews);
  assert(info.resources.bufferCopy);
  assert(info.resources.bufferFill);
  assert(info.resources.textureViews);
  assert(info.resources.bufferTextureCopy);
  assert(info.resources.textureCopy);
  assert(!info.resources.textureClear);
  assert(!info.resources.textureResolve);

  auto deviceResult = adapter.request_device({
      .requiredFeatures = {rhi::Feature::transfer, rhi::Feature::memory_budget},
  });
  assert(deviceResult.ok());
  auto device = std::move(deviceResult).value();

  constexpr std::size_t byteCount = 67;
  auto uploadResult = device.create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_source,
      .memory = rhi::MemoryDomain::upload,
      .mappedAtCreation = true,
  });
  auto deviceLocalResult = device.create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_source | rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::device_local,
  });
  auto readbackResult = device.create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  });
  assert(uploadResult.ok() && deviceLocalResult.ok() && readbackResult.ok());
  auto upload = std::move(uploadResult).value();
  auto deviceLocal = std::move(deviceLocalResult).value();
  auto readback = std::move(readbackResult).value();
  assert(device.create_buffer_view(upload).ok());

  std::array<std::byte, byteCount> expected{};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expected[index] = std::byte{static_cast<unsigned char>((index * 13u) ^ 0x6du)};
  }
  assert(upload.write(0, expected).ok());
  assert(upload.unmap().ok());

  constexpr std::uint32_t textureWidth = 8;
  constexpr std::uint32_t textureHeight = 4;
  constexpr std::size_t textureRowPitch = 256;
  constexpr std::size_t textureBytes = textureRowPitch * textureHeight;
  auto textureUploadResult = device.create_buffer({
      .size = textureBytes,
      .usage = rhi::BufferUsage::copy_source,
      .memory = rhi::MemoryDomain::upload,
  });
  auto textureReadbackResult = device.create_buffer({
      .size = textureBytes,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  });
  const rhi::TextureDesc textureDesc{
      .extent = {textureWidth, textureHeight, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_source |
               rhi::TextureUsage::copy_destination,
  };
  auto sourceTextureResult = device.create_texture(textureDesc);
  auto destinationTextureResult = device.create_texture(textureDesc);
  assert(textureUploadResult.ok() && textureReadbackResult.ok() &&
         sourceTextureResult.ok() && destinationTextureResult.ok());
  auto textureUpload = std::move(textureUploadResult).value();
  auto textureReadback = std::move(textureReadbackResult).value();
  auto sourceTexture = std::move(sourceTextureResult).value();
  auto destinationTexture = std::move(destinationTextureResult).value();
  assert(device.create_texture_view(sourceTexture).ok());
  assert(
      device
          .create_texture_view(sourceTexture, {.format = rhi::TextureFormat::rgba8_srgb})
          .ok());

  std::array<std::byte, textureBytes> expectedPixels{};
  for (std::size_t row = 0; row < textureHeight; ++row) {
    for (std::size_t columnByte = 0; columnByte < textureWidth * 4u; ++columnByte) {
      const auto index = row * textureRowPitch + columnByte;
      expectedPixels[index] =
          std::byte{static_cast<unsigned char>((index * 31u) ^ 0x5bu)};
    }
  }
  assert(textureUpload.write(0, expectedPixels).ok());

  auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
  assert(poolResult.ok());
  auto pool = std::move(poolResult).value();
  auto listResult = pool.allocate();
  assert(listResult.ok());
  auto list = std::move(listResult).value();
  assert(list.begin().ok());
  auto encoderResult = list.begin_copy();
  assert(encoderResult.ok());
  auto encoder = std::move(encoderResult).value();
  assert(encoder.copy_buffer(upload, 0, deviceLocal, 0, byteCount).ok());
  assert(encoder.copy_buffer(deviceLocal, 0, readback, 0, byteCount).ok());
  assert(encoder.fill_buffer(readback, 33, 17, std::byte{0x5a}).ok());
  assert(encoder.fill_buffer(textureReadback, 0, textureBytes, std::byte{0}).ok());
  const rhi::TextureRegion wholeTexture{
      .subresource = {.aspect = rhi::TextureAspect::color},
      .extent = {textureWidth, textureHeight, 1},
  };
  const rhi::BufferTextureCopyRegion textureRegion{
      .layout = {.bytesPerRow = textureRowPitch, .rowsPerImage = textureHeight},
      .texture = wholeTexture,
  };
  assert(
      encoder.copy_buffer_to_texture(textureUpload, sourceTexture, textureRegion).ok());
  assert(encoder
             .copy_texture(sourceTexture, destinationTexture,
                           {.source = wholeTexture, .destination = wholeTexture})
             .ok());
  assert(
      encoder.copy_texture_to_buffer(destinationTexture, textureReadback, textureRegion)
          .ok());
  assert(encoder.end().ok());
  assert(list.end().ok());

  auto queueResult = device.queue(rhi::QueueKind::graphics);
  assert(queueResult.ok());
  auto queue = std::move(queueResult).value();
  std::array<rhi::CommandList *, 1> lists{&list};
  assert(queue.submit(lists).ok());

  std::fill_n(expected.begin() + 33, 17, std::byte{0x5a});
  std::array<std::byte, byteCount> output{};
  assert(readback.read(0, output).ok());
  assert(output == expected);
  auto mappedResult = readback.map();
  assert(mappedResult.ok());
  assert(readback.invalidate().ok());
  assert(std::ranges::equal(mappedResult.value(), expected));
  assert(readback.unmap().ok());

  std::array<std::byte, textureBytes> outputPixels{};
  assert(textureReadback.read(0, outputPixels).ok());
  assert(outputPixels == expectedPixels);

  auto invalidFormatView = device.create_texture_view(
      sourceTexture, {.format = rhi::TextureFormat::bgra8_unorm});
  assert(!invalidFormatView.ok());
  assert(invalidFormatView.status().code == truffle::core::StatusCode::invalid_argument);
  auto invalidRangeView = device.create_texture_view(
      sourceTexture, {.range = {.baseMipLevel = 1, .mipLevelCount = 1}});
  assert(!invalidRangeView.ok());
  assert(invalidRangeView.status().code == truffle::core::StatusCode::invalid_argument);

  auto invalidListResult = pool.allocate();
  assert(invalidListResult.ok());
  auto invalidList = std::move(invalidListResult).value();
  assert(invalidList.begin().ok());
  auto invalidEncoderResult = invalidList.begin_copy();
  assert(invalidEncoderResult.ok());
  auto invalidEncoder = std::move(invalidEncoderResult).value();
  assert(invalidEncoder
             .copy_buffer_to_texture(
                 textureUpload, sourceTexture,
                 {.layout = {.bytesPerRow = 64, .rowsPerImage = textureHeight},
                  .texture = wholeTexture})
             .ok());
  assert(invalidEncoder.end().ok());
  assert(invalidList.end().ok());
  std::array<rhi::CommandList *, 1> invalidLists{&invalidList};
  const auto invalidSubmit = queue.submit(invalidLists);
  assert(!invalidSubmit.ok());
  assert(invalidSubmit.code == truffle::core::StatusCode::invalid_argument);

  auto arrayTexture = device.create_texture({
      .extent = {8, 8, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::sampled,
      .mipLevels = 3,
      .arrayLayers = 2,
  });
  assert(arrayTexture.ok());
  assert(device
             .create_texture_view(arrayTexture.value(), {.range = {.baseMipLevel = 1,
                                                                   .mipLevelCount = 2,
                                                                   .baseArrayLayer = 1,
                                                                   .arrayLayerCount = 1}})
             .ok());

  auto cubeTexture = device.create_texture({
      .dimension = rhi::TextureDimension::cube,
      .extent = {4, 4, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::sampled,
      .arrayLayers = 6,
  });
  assert(cubeTexture.ok());
  auto cubeView = device.create_texture_view(
      cubeTexture.value(),
      {.dimension = rhi::TextureDimension::cube, .range = {.arrayLayerCount = 6}});
  assert(cubeView.ok());

  auto depthTexture = device.create_texture({
      .extent = {4, 4, 1},
      .format = rhi::TextureFormat::depth32_float,
      .usage = rhi::TextureUsage::depth_stencil_attachment,
  });
  assert(depthTexture.ok());
  assert(device
             .create_texture_view(depthTexture.value(),
                                  {.range = {.aspects = rhi::TextureAspect::depth}})
             .ok());

  auto unsupportedTexture = device.create_texture({
      .extent = {4, 4, 1},
      .format = rhi::TextureFormat::bc1_rgba_unorm,
      .usage = rhi::TextureUsage::copy_source,
  });
  assert(!unsupportedTexture.ok());
  assert(unsupportedTexture.status().code == truffle::core::StatusCode::unsupported);

  auto multisampleAttachment = device.create_texture({
      .extent = {4, 4, 1},
      .usage = rhi::TextureUsage::color_attachment,
      .sampleCount = 4,
  });
  assert(multisampleAttachment.ok());
}

void verify_direct3d_synchronization() {
  auto instanceResult = rhi::create_direct3d12_instance();
  assert(instanceResult.ok());
  auto instance = std::move(instanceResult).value();
  auto adapter = instance.adapter(0);
  assert(adapter.ok());
  auto device = adapter.value().request_device({.requiredFeatures = {
                                                    rhi::Feature::transfer,
                                                }});
  assert(device.ok());
  auto queue = device.value().queue(rhi::QueueKind::graphics);
  auto pool = device.value().create_command_pool(rhi::QueueKind::graphics);
  assert(queue.ok() && pool.ok());

  constexpr std::size_t byteCount = 64;
  std::array<std::byte, byteCount> expected{};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expected[index] = std::byte{static_cast<unsigned char>(index * 7u + 3u)};
  }
  auto upload = device.value().create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_source,
      .memory = rhi::MemoryDomain::upload,
  });
  auto intermediate = device.value().create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_source | rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::device_local,
  });
  auto readback = device.value().create_buffer({
      .size = byteCount,
      .usage = rhi::BufferUsage::copy_destination,
      .memory = rhi::MemoryDomain::readback,
  });
  assert(upload.ok() && intermediate.ok() && readback.ok());
  assert(upload.value().write(0, expected).ok());

  auto first = pool.value().allocate();
  auto second = pool.value().allocate();
  assert(first.ok() && second.ok());
  assert(first.value().begin().ok());
  auto firstCopy = first.value().begin_copy();
  assert(firstCopy.ok());
  assert(firstCopy.value()
             .copy_buffer(upload.value(), 0, intermediate.value(), 0, byteCount)
             .ok());
  assert(firstCopy.value().end().ok());
  rhi::BarrierBatch copyBarrier;
  copyBarrier.buffers.push_back({
      .buffer = &intermediate.value(),
      .sourceStages = rhi::PipelineStage::copy,
      .destinationStages = rhi::PipelineStage::copy,
      .sourceAccess = rhi::Access::transfer_write,
      .destinationAccess = rhi::Access::transfer_read,
  });
  assert(first.value().barrier(copyBarrier).ok());
  assert(first.value().end().ok());

  assert(second.value().begin().ok());
  auto secondCopy = second.value().begin_copy();
  assert(secondCopy.ok());
  assert(secondCopy.value()
             .copy_buffer(intermediate.value(), 0, readback.value(), 0, byteCount)
             .ok());
  assert(secondCopy.value().end().ok());
  assert(second.value().end().ok());

  auto timeline = device.value().create_semaphore();
  auto completion = device.value().create_fence();
  assert(timeline.ok() && completion.ok());
  std::array<rhi::CommandList *, 2> ordered{&first.value(), &second.value()};
  const std::array<rhi::SemaphoreSignal, 1> signal{{
      {.semaphore = &timeline.value(), .value = 4},
  }};
  assert(queue.value()
             .submit({.commandLists = ordered,
                      .signals = signal,
                      .signalFence = &completion.value(),
                      .signalFenceValue = 7})
             .ok());
  assert(timeline.value().value() == 4);
  assert(completion.value().completed_value() == 7);
  assert(completion.value().wait(7, std::chrono::nanoseconds{1}).ok());
  std::array<std::byte, byteCount> output{};
  assert(readback.value().read(0, output).ok());
  assert(output == expected);

  auto waiting = pool.value().allocate();
  assert(waiting.ok());
  assert(waiting.value().begin().ok());
  assert(waiting.value().end().ok());
  auto blocked = device.value().create_semaphore();
  assert(blocked.ok());
  std::array<rhi::CommandList *, 1> waitingLists{&waiting.value()};
  const std::array<rhi::SemaphoreWait, 1> waits{{
      {.semaphore = &blocked.value(), .value = 1},
  }};
  const auto timeout = queue.value().submit({
      .commandLists = waitingLists,
      .waits = waits,
      .waitTimeout = std::chrono::nanoseconds{1},
  });
  assert(!timeout.ok());
  assert(timeout.code == truffle::core::StatusCode::timeout);
  assert(waiting.value().state() == rhi::CommandListState::executable);

  auto signalList = pool.value().allocate();
  assert(signalList.ok());
  assert(signalList.value().begin().ok());
  assert(signalList.value().end().ok());
  std::array<rhi::CommandList *, 1> signalLists{&signalList.value()};
  const std::array<rhi::SemaphoreSignal, 1> release{{
      {.semaphore = &blocked.value(), .value = 1},
  }};
  assert(queue.value().submit({.commandLists = signalLists, .signals = release}).ok());
  assert(queue.value()
             .submit({.commandLists = waitingLists,
                      .waits = waits,
                      .waitTimeout = std::chrono::milliseconds{1}})
             .ok());

  auto texture = device.value().create_texture({
      .extent = {4, 4, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_destination,
  });
  assert(texture.ok());
  auto transitionList = pool.value().allocate();
  assert(transitionList.ok());
  assert(transitionList.value().begin().ok());
  rhi::BarrierBatch transition;
  transition.textures.push_back({
      .texture = &texture.value(),
      .range = {.aspects = rhi::TextureAspect::color},
      .oldLayout = rhi::TextureLayout::undefined,
      .newLayout = rhi::TextureLayout::shader_read_only,
      .destinationStages = rhi::PipelineStage::fragment_shader,
      .destinationAccess = rhi::Access::shader_read,
  });
  assert(transitionList.value().barrier(transition).ok());
  assert(transitionList.value().end().ok());
  std::array<rhi::CommandList *, 1> transitionLists{&transitionList.value()};
  assert(queue.value().submit(transitionLists).ok());

  auto staleList = pool.value().allocate();
  assert(staleList.ok());
  assert(staleList.value().begin().ok());
  rhi::BarrierBatch stale;
  stale.textures.push_back({
      .texture = &texture.value(),
      .range = {.aspects = rhi::TextureAspect::color},
      .oldLayout = rhi::TextureLayout::undefined,
      .newLayout = rhi::TextureLayout::general,
  });
  assert(staleList.value().barrier(stale).ok());
  assert(staleList.value().end().ok());
  std::array<rhi::CommandList *, 1> staleLists{&staleList.value()};
  const auto staleSubmit = queue.value().submit(staleLists);
  assert(!staleSubmit.ok());
  assert(staleSubmit.code == truffle::core::StatusCode::invalid_state);

  auto aliasList = pool.value().allocate();
  assert(aliasList.ok());
  assert(aliasList.value().begin().ok());
  rhi::BarrierBatch invalidAlias;
  invalidAlias.aliasing.push_back({
      .beforeBuffer = &intermediate.value(),
      .afterBuffer = &intermediate.value(),
  });
  const auto aliasStatus = aliasList.value().barrier(invalidAlias);
  assert(!aliasStatus.ok());
  assert(aliasStatus.code == truffle::core::StatusCode::invalid_argument);
}

void verify_direct3d_bindings_depth_and_compute() {
  constexpr std::string_view shaderSource = R"(
Texture2D textures[2] : register(t0, space0);
SamplerState textureSampler : register(s2, space0);
cbuffer Tint : register(b3, space0) { float4 tint; };
cbuffer PushConstants : register(b255, space0) {
  float depthValue;
  float3 unusedPadding;
  float4 pushedColor;
};
RWByteAddressBuffer computeOutput : register(u4, space0);
ByteAddressBuffer renderInput : register(t4, space0);

float4 fullscreen_vs(uint vertexId : SV_VertexID) : SV_Position {
  const float2 positions[3] = {
      float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
  return float4(positions[vertexId], 0.5, 1.0);
}

float4 depth_vs(uint vertexId : SV_VertexID) : SV_Position {
  const float2 positions[3] = {
      float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
  return float4(positions[vertexId], depthValue, 1.0);
}

float4 textured_ps() : SV_Target0 {
  return textures[1].SampleLevel(textureSampler, float2(0.5, 0.5), 0) * tint;
}

float4 pushed_ps() : SV_Target0 { return pushedColor; }

[numthreads(1, 1, 1)]
void compute_main(uint3 id : SV_DispatchThreadID) {
  computeOutput.Store4(0, asuint(float4(0.0, 0.0, 1.0, 1.0)));
}

float4 computed_ps() : SV_Target0 {
  return asfloat(renderInput.Load4(0));
}
)";
  auto instance = rhi::create_direct3d12_instance();
  assert(instance.ok());
  auto adapter = instance.value().adapter(0);
  assert(adapter.ok());
  auto device = adapter.value().request_device({
      .requiredFeatures = {rhi::Feature::compute, rhi::Feature::transfer,
                           rhi::Feature::descriptor_arrays, rhi::Feature::dynamic_offsets,
                           rhi::Feature::push_constants},
  });
  assert(device.ok());
  auto queue = device.value().queue(rhi::QueueKind::graphics);
  assert(queue.ok());
  auto vertex = make_hlsl_shader(device.value(), "D3D12 binding vertex",
                                 rhi::ShaderStage::vertex, "fullscreen_vs", shaderSource);

  auto textureUpload = device.value().create_buffer({
      .size = 256,
      .usage = rhi::BufferUsage::copy_source,
      .memory = rhi::MemoryDomain::upload,
  });
  auto sampledTexture = device.value().create_texture({
      .extent = {1, 1, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_destination,
  });
  assert(textureUpload.ok() && sampledTexture.ok());
  std::array<std::byte, 256> texelUpload{};
  texelUpload[0] = std::byte{10};
  texelUpload[1] = std::byte{20};
  texelUpload[2] = std::byte{30};
  texelUpload[3] = std::byte{255};
  assert(textureUpload.value().write(0, texelUpload).ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto copy = list.begin_copy();
    assert(copy.ok());
    assert(copy.value()
               .copy_buffer_to_texture(
                   textureUpload.value(), sampledTexture.value(),
                   {.layout = {.bytesPerRow = 256, .rowsPerImage = 1},
                    .texture = {.subresource = {.aspect = rhi::TextureAspect::color},
                                .extent = {1, 1, 1}}})
               .ok());
    assert(copy.value().end().ok());
  });
  auto sampledView = device.value().create_texture_view(sampledTexture.value());
  auto sampler = device.value().create_sampler();
  assert(sampledView.ok() && sampler.ok());
  auto bindingLayout = device.value().create_bind_group_layout({
      .group = 0,
      .entries = {{.binding = 0,
                   .type = rhi::BindingType::sampled_texture,
                   .arrayCount = 2,
                   .visibility = rhi::ShaderStageMask::fragment},
                  {.binding = 1,
                   .type = rhi::BindingType::sampler,
                   .visibility = rhi::ShaderStageMask::fragment,
                   .immutableSampler = &sampler.value()},
                  {.binding = 2,
                   .type = rhi::BindingType::uniform_buffer,
                   .visibility = rhi::ShaderStageMask::fragment,
                   .dynamicOffset = true,
                   .minimumBufferSize = 16}},
  });
  assert(bindingLayout.ok());
  auto pipelineLayout = device.value().create_pipeline_layout(
      {.bindGroupLayouts = {&bindingLayout.value()}});
  assert(pipelineLayout.ok());
  auto tint = device.value().create_buffer({
      .size = 512,
      .usage = rhi::BufferUsage::uniform,
      .memory = rhi::MemoryDomain::upload,
  });
  assert(tint.ok());
  const std::array<float, 4> halfRed{0.5F, 1.0F, 1.0F, 1.0F};
  assert(tint.value().write(256, std::as_bytes(std::span{halfRed})).ok());
  auto arena = device.value().create_descriptor_arena();
  assert(arena.ok());
  auto group = device.value().create_bind_group({
      .layout = &bindingLayout.value(),
      .arena = &arena.value(),
      .entries = {{.binding = 0, .arrayElement = 0, .textureView = &sampledView.value()},
                  {.binding = 0, .arrayElement = 1, .textureView = &sampledView.value()},
                  {.binding = 2, .buffer = &tint.value(), .size = 16}},
  });
  assert(group.ok());
  std::vector<rhi::ResourceBinding> texturedReflection{
      {.name = "textures",
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
       .minimumSize = 16},
  };
  std::vector<rhi::ShaderBindingMap> texturedMap{
      {.stage = rhi::ShaderStage::fragment, .group = 0, .binding = 0, .nativeBinding = 0},
      {.stage = rhi::ShaderStage::fragment, .group = 0, .binding = 1, .nativeBinding = 2},
      {.stage = rhi::ShaderStage::fragment, .group = 0, .binding = 2, .nativeBinding = 3},
  };
  auto texturedFragment = make_hlsl_shader(
      device.value(), "D3D12 textured fragment", rhi::ShaderStage::fragment,
      "textured_ps", shaderSource, texturedReflection, {}, texturedMap);
  auto texturedPipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &texturedFragment,
      .layout = &pipelineLayout.value(),
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
  });
  assert(texturedPipeline.ok());
  constexpr std::uint32_t width = 8;
  constexpr std::uint32_t height = 8;
  auto target = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
  });
  assert(target.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render = list.begin_rendering(
        {.extent = {width, height}, .colorAttachments = {{.texture = &target.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(texturedPipeline.value()).ok());
    assert(render.value()
               .bind_group(0, group.value(), std::array<std::uint32_t, 1>{256})
               .ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), target.value(), width, height), width,
      height, {std::byte{5}, std::byte{20}, std::byte{30}, std::byte{255}});

  auto depthVertex = make_hlsl_shader(
      device.value(), "D3D12 depth vertex", rhi::ShaderStage::vertex, "depth_vs",
      shaderSource, {}, {{.stage = rhi::ShaderStage::vertex, .offset = 0, .size = 4}});
  auto pushedFragment =
      make_hlsl_shader(device.value(), "D3D12 pushed fragment",
                       rhi::ShaderStage::fragment, "pushed_ps", shaderSource, {},
                       {{.stage = rhi::ShaderStage::fragment, .offset = 16, .size = 16}});
  auto pushLayout = device.value().create_pipeline_layout({
      .pushConstants = {{.stage = rhi::ShaderStage::vertex, .offset = 0, .size = 4},
                        {.stage = rhi::ShaderStage::fragment, .offset = 16, .size = 16}},
  });
  assert(pushLayout.ok());
  auto depthPipeline = device.value().create_pipeline({
      .vertexShader = &depthVertex,
      .fragmentShader = &pushedFragment,
      .layout = &pushLayout.value(),
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
      .depthStencil = {.format = rhi::TextureFormat::depth32_float,
                       .depthWriteEnabled = true,
                       .depthCompare = rhi::CompareOp::less},
  });
  auto depth = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::depth32_float,
      .usage = rhi::TextureUsage::depth_stencil_attachment,
  });
  assert(depthPipeline.ok() && depth.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render =
        list.begin_rendering({.extent = {width, height},
                              .colorAttachments = {{.texture = &target.value()}},
                              .depthStencilAttachment = {.texture = &depth.value()}});
    assert(render.ok());
    assert(render.value().bind_pipeline(depthPipeline.value()).ok());
    const float nearDepth = 0.25F;
    const std::array<float, 4> green{0.0F, 1.0F, 0.0F, 1.0F};
    assert(render.value()
               .push_constants(rhi::ShaderStageMask::vertex, 0,
                               std::as_bytes(std::span{&nearDepth, 1}))
               .ok());
    assert(render.value()
               .push_constants(rhi::ShaderStageMask::fragment, 16,
                               std::as_bytes(std::span{green}))
               .ok());
    assert(render.value().draw(3).ok());
    const float farDepth = 0.75F;
    const std::array<float, 4> red{1.0F, 0.0F, 0.0F, 1.0F};
    assert(render.value()
               .push_constants(rhi::ShaderStageMask::vertex, 0,
                               std::as_bytes(std::span{&farDepth, 1}))
               .ok());
    assert(render.value()
               .push_constants(rhi::ShaderStageMask::fragment, 16,
                               std::as_bytes(std::span{red}))
               .ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), target.value(), width, height), width,
      height, {std::byte{}, std::byte{255}, std::byte{}, std::byte{255}});

  auto storageLayout = device.value().create_bind_group_layout({
      .group = 0,
      .entries = {{.binding = 0,
                   .type = rhi::BindingType::storage_buffer,
                   .visibility =
                       rhi::ShaderStageMask::compute | rhi::ShaderStageMask::fragment,
                   .minimumBufferSize = 16}},
  });
  assert(storageLayout.ok());
  auto storagePipelineLayout = device.value().create_pipeline_layout(
      {.bindGroupLayouts = {&storageLayout.value()}});
  auto storage = device.value().create_buffer({
      .size = 16,
      .usage = rhi::BufferUsage::storage,
      .memory = rhi::MemoryDomain::device_local,
  });
  auto storageArena = device.value().create_descriptor_arena();
  assert(storagePipelineLayout.ok() && storage.ok() && storageArena.ok());
  auto storageGroup = device.value().create_bind_group({
      .layout = &storageLayout.value(),
      .arena = &storageArena.value(),
      .entries = {{.binding = 0, .buffer = &storage.value(), .size = 16}},
  });
  assert(storageGroup.ok());
  const std::vector<rhi::ShaderBindingMap> storageMap{{
      .stage = rhi::ShaderStage::compute,
      .group = 0,
      .binding = 0,
      .nativeBinding = 4,
  }};
  auto computeShader =
      make_hlsl_shader(device.value(), "D3D12 compute", rhi::ShaderStage::compute,
                       "compute_main", shaderSource,
                       {{.name = "computeOutput",
                         .stage = rhi::ShaderStage::compute,
                         .type = rhi::ResourceBindingType::buffer,
                         .group = 0,
                         .binding = 0,
                         .minimumSize = 16,
                         .readOnly = false}},
                       {}, storageMap);
  auto computePipeline = device.value().create_compute_pipeline({
      .computeShader = &computeShader,
      .layout = &storagePipelineLayout.value(),
      .requiredWorkgroupSize = {1, 1, 1},
  });
  assert(computePipeline.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto compute = list.begin_compute();
    assert(compute.ok());
    assert(compute.value().bind_pipeline(computePipeline.value()).ok());
    assert(compute.value().bind_group(0, storageGroup.value()).ok());
    assert(compute.value().dispatch(1, 1, 1).ok());
    assert(compute.value().end().ok());
  });
  auto computedFragment =
      make_hlsl_shader(device.value(), "D3D12 computed fragment",
                       rhi::ShaderStage::fragment, "computed_ps", shaderSource,
                       {{.name = "renderInput",
                         .stage = rhi::ShaderStage::fragment,
                         .type = rhi::ResourceBindingType::buffer,
                         .group = 0,
                         .binding = 0,
                         .minimumSize = 16}},
                       {},
                       {{.stage = rhi::ShaderStage::fragment,
                         .group = 0,
                         .binding = 0,
                         .nativeBinding = 4}});
  auto computedPipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &computedFragment,
      .layout = &storagePipelineLayout.value(),
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
  });
  assert(computedPipeline.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render = list.begin_rendering(
        {.extent = {width, height}, .colorAttachments = {{.texture = &target.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(computedPipeline.value()).ok());
    assert(render.value().bind_group(0, storageGroup.value()).ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), target.value(), width, height), width,
      height, {std::byte{}, std::byte{}, std::byte{255}, std::byte{255}});

  auto mismatchedLayout = device.value().create_bind_group_layout({
      .group = 0,
      .entries = {{.binding = 0, .type = rhi::BindingType::sampler}},
  });
  assert(mismatchedLayout.ok());
  auto mismatchedPipelineLayout = device.value().create_pipeline_layout(
      {.bindGroupLayouts = {&mismatchedLayout.value()}});
  assert(mismatchedPipelineLayout.ok());
  auto invalidPipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &computedFragment,
      .layout = &mismatchedPipelineLayout.value(),
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
  });
  assert(!invalidPipeline.ok());
  assert(invalidPipeline.status().code == truffle::core::StatusCode::invalid_argument);
  auto unsupportedDynamicBias = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &computedFragment,
      .layout = &storagePipelineLayout.value(),
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
      .dynamicState = rhi::DynamicState::depth_bias,
  });
  assert(!unsupportedDynamicBias.ok());
  assert(unsupportedDynamicBias.status().code == truffle::core::StatusCode::unsupported);
  assert(!device.value().create_pipeline_cache().ok());
  assert(!device.value()
              .create_bindless_table({.layout = &storageLayout.value(), .capacity = 8})
              .ok());
}

void verify_direct3d_graphics_output() {
  constexpr std::string_view shaderSource = R"(
struct MrtOutput {
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};

struct VertexInput {
  float2 position : TEXCOORD0;
  float4 color : TEXCOORD1;
};

struct VertexOutput {
  float4 position : SV_Position;
  float4 color : TEXCOORD0;
};

float4 fullscreen_vs(uint vertexId : SV_VertexID) : SV_Position {
  const float2 positions[3] = {
      float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)};
  return float4(positions[vertexId], 0.5, 1.0);
}

float4 solid_ps() : SV_Target0 { return float4(0.25, 0.5, 0.75, 1.0); }

VertexOutput vertex_input_vs(VertexInput input) {
  VertexOutput output;
  output.position = float4(input.position, 0.5, 1.0);
  output.color = input.color;
  return output;
}

float4 vertex_color_ps(VertexOutput input) : SV_Target0 { return input.color; }

MrtOutput mrt_ps() {
  MrtOutput output;
  output.first = float4(1.0, 0.0, 0.0, 1.0);
  output.second = float4(0.0, 1.0, 0.0, 1.0);
  return output;
}
)";
  auto instance = rhi::create_direct3d12_instance();
  assert(instance.ok());
  auto adapter = instance.value().adapter(0);
  assert(adapter.ok());
  const auto &info = adapter.value().info();
  assert(info.bindings.ordinaryBindGroups);
  assert(info.bindings.descriptorArrays);
  assert(info.bindings.dynamicOffsets);
  assert(info.bindings.immutableSamplers);
  assert(info.bindings.pushConstants);
  assert(info.pipelines.graphics && info.pipelines.compute);
  assert(info.pipelines.multipleRenderTargets && info.pipelines.depthStencil);
  assert(info.pipelines.multisample && info.pipelines.indirect);
  assert(!info.pipelines.tessellation && !info.pipelines.indirectCount &&
         !info.pipelines.pipelineCache);

  auto device = adapter.value().request_device(
      {.requiredFeatures = {
           rhi::Feature::compute, rhi::Feature::transfer, rhi::Feature::descriptor_arrays,
           rhi::Feature::dynamic_offsets, rhi::Feature::push_constants}});
  assert(device.ok());
  auto queue = device.value().queue(rhi::QueueKind::graphics);
  assert(queue.ok());
  auto vertex = make_hlsl_shader(device.value(), "D3D12 fullscreen vertex",
                                 rhi::ShaderStage::vertex, "fullscreen_vs", shaderSource);
  auto fragment = make_hlsl_shader(device.value(), "D3D12 solid fragment",
                                   rhi::ShaderStage::fragment, "solid_ps", shaderSource);
  auto mrtFragment = make_hlsl_shader(device.value(), "D3D12 MRT fragment",
                                      rhi::ShaderStage::fragment, "mrt_ps", shaderSource);

  auto pipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &fragment,
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
  });
  assert(pipeline.ok());
  constexpr std::uint32_t width = 8;
  constexpr std::uint32_t height = 8;
  auto color = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
  });
  assert(color.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render =
        list.begin_rendering({.extent = {width, height},
                              .colorAttachments = {{.texture = &color.value(),
                                                    .loadOp = rhi::LoadOp::clear,
                                                    .storeOp = rhi::StoreOp::store}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(pipeline.value()).ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), color.value(), width, height), width,
      height, {std::byte{64}, std::byte{128}, std::byte{191}, std::byte{255}});

  auto vertexInput =
      make_hlsl_shader(device.value(), "D3D12 vertex input", rhi::ShaderStage::vertex,
                       "vertex_input_vs", shaderSource);
  auto vertexColor =
      make_hlsl_shader(device.value(), "D3D12 vertex color", rhi::ShaderStage::fragment,
                       "vertex_color_ps", shaderSource);
  auto vertexPipeline = device.value().create_pipeline({
      .vertexShader = &vertexInput,
      .fragmentShader = &vertexColor,
      .vertexBuffers =
          {{.stride = 24,
            .attributes =
                {{.location = 0, .format = rhi::VertexFormat::float32x2, .offset = 0},
                 {.location = 1, .format = rhi::VertexFormat::float32x4, .offset = 8}}}},
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm,
                        .blend = {.enabled = true}}},
  });
  const std::array<float, 18> vertexBytes{
      -1.0F, -1.0F, 0.25F, 0.5F, 0.75F, 1.0F,  -1.0F, 3.0F,  0.25F,
      0.5F,  0.75F, 1.0F,  3.0F, -1.0F, 0.25F, 0.5F,  0.75F, 1.0F,
  };
  auto vertexBuffer = device.value().create_buffer({
      .size = sizeof(vertexBytes),
      .usage = rhi::BufferUsage::vertex,
      .memory = rhi::MemoryDomain::upload,
  });
  assert(vertexPipeline.ok() && vertexBuffer.ok());
  assert(vertexBuffer.value().write(0, std::as_bytes(std::span{vertexBytes})).ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render = list.begin_rendering(
        {.extent = {width, height}, .colorAttachments = {{.texture = &color.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(vertexPipeline.value()).ok());
    assert(render.value().bind_vertex_buffer(0, vertexBuffer.value()).ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), color.value(), width, height), width,
      height, {std::byte{64}, std::byte{128}, std::byte{191}, std::byte{255}});

  auto index = device.value().create_buffer({
      .size = 3 * sizeof(std::uint16_t),
      .usage = rhi::BufferUsage::index,
      .memory = rhi::MemoryDomain::upload,
  });
  auto indirect = device.value().create_buffer({
      .size = 4 * sizeof(std::uint32_t),
      .usage = rhi::BufferUsage::indirect,
      .memory = rhi::MemoryDomain::upload,
  });
  assert(index.ok() && indirect.ok());
  const std::array<std::uint16_t, 3> indices{0, 1, 2};
  const std::array<std::uint32_t, 4> drawArguments{3, 2, 0, 0};
  assert(index.value().write(0, std::as_bytes(std::span{indices})).ok());
  assert(indirect.value().write(0, std::as_bytes(std::span{drawArguments})).ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render = list.begin_rendering(
        {.extent = {width, height}, .colorAttachments = {{.texture = &color.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(pipeline.value()).ok());
    assert(render.value()
               .bind_index_buffer(index.value(), 0, rhi::IndexFormat::uint16)
               .ok());
    assert(render.value().draw_indexed(3, 2).ok());
    assert(render.value().draw_indirect(indirect.value(), 0, false).ok());
    assert(render.value().end().ok());
  });

  auto mrtPipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &mrtFragment,
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm},
                       {.format = rhi::TextureFormat::rgba8_unorm}},
  });
  auto secondColor = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
  });
  assert(mrtPipeline.ok() && secondColor.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render =
        list.begin_rendering({.extent = {width, height},
                              .colorAttachments = {{.texture = &color.value()},
                                                   {.texture = &secondColor.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(mrtPipeline.value()).ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), color.value(), width, height), width,
      height, {std::byte{255}, std::byte{}, std::byte{}, std::byte{255}});
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), secondColor.value(), width, height),
      width, height, {std::byte{}, std::byte{255}, std::byte{}, std::byte{255}});

  auto multisample = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::color_attachment,
      .sampleCount = 4,
  });
  auto resolve = device.value().create_texture({
      .extent = {width, height, 1},
      .format = rhi::TextureFormat::rgba8_unorm,
      .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
  });
  auto multisamplePipeline = device.value().create_pipeline({
      .vertexShader = &vertex,
      .fragmentShader = &fragment,
      .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
      .multisample = {.sampleCount = 4},
  });
  assert(multisample.ok() && resolve.ok() && multisamplePipeline.ok());
  submit(device.value(), queue.value(), [&](rhi::CommandList &list) {
    auto render = list.begin_rendering(
        {.extent = {width, height},
         .colorAttachments = {
             {.texture = &multisample.value(), .resolveTexture = &resolve.value()}}});
    assert(render.ok());
    assert(render.value().bind_pipeline(multisamplePipeline.value()).ok());
    assert(render.value().draw(3).ok());
    assert(render.value().end().ok());
  });
  assert_solid_rgba8(
      read_rgba8(device.value(), queue.value(), resolve.value(), width, height), width,
      height, {std::byte{64}, std::byte{128}, std::byte{191}, std::byte{255}});
}

} // namespace

int main() {
#ifdef _WIN32
  verify_direct3d_buffers();
  verify_direct3d_synchronization();
  verify_direct3d_graphics_output();
  verify_direct3d_bindings_depth_and_compute();
#else
  const auto result = truffle::rhi::create_direct3d12_instance();
  truffle::tests::verify_unavailable_backend(result);
#endif
  return 0;
}

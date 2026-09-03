#include "truffle/rhi/direct3d_backend.hpp"

#include "native_backend_smoke.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

namespace {

void verify_direct3d_buffers() {
  namespace rhi = truffle::rhi;

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
  assert(device.create_texture_view(cubeTexture.value()).ok());

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

  auto unsupportedMultisample = device.create_texture({
      .extent = {4, 4, 1},
      .usage = rhi::TextureUsage::color_attachment,
      .sampleCount = 4,
  });
  assert(!unsupportedMultisample.ok());
  assert(unsupportedMultisample.status().code == truffle::core::StatusCode::unsupported);
}

} // namespace

int main() {
#ifdef _WIN32
  verify_direct3d_buffers();
#else
  const auto result = truffle::rhi::create_direct3d12_instance();
  truffle::tests::verify_unavailable_backend(result);
#endif
  return 0;
}

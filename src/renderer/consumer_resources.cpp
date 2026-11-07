#include "consumer_convert.hpp"
#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/specialist/depth_target.hpp"
#include "truffle/renderer/specialist/mesh.hpp"

#include <cassert>

namespace truffle::renderer {

struct Device::State {
  rhi::Instance instance;
  rhi::Device owned;
  rhi::Device *device = nullptr;
};
base::Result<Device> RhiInterop::borrow(rhi::Device &raw) {
  if (!raw.valid())
    return detail::invalid("Cannot borrow an invalid graphics device");
  Device result;
  result.state_ = std::make_shared<Device::State>();
  result.state_->device = &raw;
  return result;
}
base::Result<Device> RhiInterop::adopt(rhi::Instance instance, rhi::Device raw) {
  if (!instance.valid() || !raw.valid())
    return detail::invalid("Cannot adopt invalid graphics owners");
  Device result;
  result.state_ = std::make_shared<Device::State>();
  result.state_->instance = std::move(instance);
  result.state_->owned = std::move(raw);
  result.state_->device = &result.state_->owned;
  return result;
}
Device RhiInterop::share(Device &device) {
  Device result;
  result.state_ = device.state_;
  return result;
}
rhi::Device &RhiInterop::native(Device &device) {
  assert(device.valid());
  return *device.state_->device;
}
bool Device::valid() const noexcept { return state_ && state_->device->valid(); }
bool Device::lost() const noexcept { return valid() && state_->device->lost(); }

struct BufferSlice::State {
  specialist::BufferView raw;
  std::shared_ptr<void> owner;
};
BufferSlice RhiInterop::buffer(specialist::BufferView raw, std::shared_ptr<void> owner) {
  BufferSlice result;
  result.state_ =
      std::make_shared<BufferSlice::State>(BufferSlice::State{raw, std::move(owner)});
  return result;
}
specialist::BufferView RhiInterop::native(const BufferSlice &value) {
  return value.state_ ? value.state_->raw : specialist::BufferView{};
}
bool BufferSlice::valid() const noexcept {
  return state_ && state_->raw.buffer && state_->raw.buffer->valid();
}
std::size_t BufferSlice::size() const noexcept { return state_ ? state_->raw.size : 0; }
struct Buffer::State {
  Device device;
  rhi::Buffer buffer;
};
base::Result<Buffer> Buffer::upload(Device &device, std::span<const std::byte> bytes,
                                    BufferUsage usage) {
  if (!device.valid())
    return detail::invalid("Buffer upload requires a device");
  if (bytes.empty() || usage == BufferUsage::none ||
      (static_cast<unsigned>(usage) & ~31u) != 0) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Buffer upload requires bytes and valid usage");
  }
  auto created = RhiInterop::native(device).create_buffer({
      .size = bytes.size(),
      .usage = detail::enum_value<rhi::BufferUsage>(usage),
      .memory = rhi::MemoryDomain::upload,
      .debugName = "Renderer buffer",
  });
  if (!created.ok())
    return created.status();
  if (auto status = created.value().write(0, bytes); !status.ok())
    return status;
  Buffer result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->buffer = std::move(created).value();
  return result;
}
base::Result<BufferSlice> Buffer::slice(std::size_t offset, std::size_t bytes) const {
  if (!state_)
    return detail::invalid("Buffer is not created");
  const auto capacity = state_->buffer.desc().size;
  if (offset >= capacity || bytes > capacity - offset) {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Buffer slice is outside its storage");
  }
  return RhiInterop::buffer(
      {&state_->buffer, offset, bytes == 0 ? capacity - offset : bytes}, state_);
}

struct Texture::State {
  rhi::Texture *texture;
  rhi::TextureView *view;
  std::shared_ptr<void> owner;
};
Texture RhiInterop::texture(rhi::Texture &texture, rhi::TextureView *view,
                            std::shared_ptr<void> owner) {
  Texture result;
  result.state_ =
      std::make_shared<Texture::State>(Texture::State{&texture, view, std::move(owner)});
  return result;
}
rhi::Texture *RhiInterop::native(const Texture &value) {
  return value.state_ ? value.state_->texture : nullptr;
}
rhi::TextureView *RhiInterop::view(const Texture &value) {
  return value.state_ ? value.state_->view : nullptr;
}
bool Texture::valid() const noexcept { return state_ && state_->texture->valid(); }
PixelSize Texture::extent() const noexcept {
  if (!valid())
    return {};
  const auto extent = state_->texture->desc().extent;
  return {extent.width, extent.height};
}
TextureFormat Texture::format() const noexcept {
  if (!valid())
    return TextureFormat::unknown;
  const auto format = detail::enum_value<TextureFormat>(state_->texture->desc().format);
  return detail::valid_format(format) ? format : TextureFormat::unknown;
}
std::uint64_t Texture::id() const noexcept {
  return valid() ? state_->texture->id().value : 0;
}
struct SampledTexture::State {
  Device device;
  specialist::SampledTexture raw;
};
specialist::SampledTexture &RhiInterop::native(SampledTexture &value) {
  assert(value.state_);
  return value.state_->raw;
}
base::Result<SampledTexture> SampledTexture::upload(Device &device,
                                                    const TextureUpload &desc) {
  if (!device.valid())
    return detail::invalid("Texture upload requires a device");
  if (!detail::valid_format(desc.format))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer texture format");
  auto raw = specialist::SampledTexture::upload(
      RhiInterop::native(device),
      {
          .extent = detail::extent(desc.extent),
          .format = detail::enum_value<rhi::TextureFormat>(desc.format),
          .bytes = desc.bytes,
          .bytesPerRow = desc.bytesPerRow,
          .debugName = desc.debugName,
      });
  if (!raw.ok())
    return raw.status();
  SampledTexture result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  return result;
}
base::Result<SampledTexture> SampledTexture::render_target(Device &device,
                                                           const RenderTargetDesc &desc) {
  if (!device.valid())
    return detail::invalid("Render target requires a device");
  if (!detail::valid_format(desc.format))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer texture format");
  auto raw = specialist::SampledTexture::render_target(
      RhiInterop::native(device),
      {
          .extent = detail::extent(desc.extent),
          .format = detail::enum_value<rhi::TextureFormat>(desc.format),
          .debugName = desc.debugName,
      });
  if (!raw.ok())
    return raw.status();
  SampledTexture result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  return result;
}
base::Status SampledTexture::update(Device &device, PixelOrigin origin, PixelSize extent,
                                    std::span<const std::byte> bytes,
                                    std::size_t bytesPerRow) {
  if (!state_ || !device.valid() ||
      &RhiInterop::native(device) != &RhiInterop::native(state_->device)) {
    return detail::invalid("Texture update must use its original device");
  }
  return state_->raw.update(RhiInterop::native(device), {origin.x, origin.y, 0},
                            detail::extent(extent), bytes, bytesPerRow);
}
Texture SampledTexture::texture() const {
  return state_ ? RhiInterop::texture(state_->raw.texture(), &state_->raw.view(), state_)
                : Texture{};
}
TextureTransition SampledTexture::to_attachment() const {
  return {texture(), TextureUse::sampled, TextureUse::color_attachment};
}
TextureTransition SampledTexture::to_shader_read() const {
  return {texture(), TextureUse::color_attachment, TextureUse::sampled};
}

struct Mesh::State {
  Device device;
  specialist::Mesh raw;
};
base::Result<Mesh> Mesh::upload(Device &device, const MeshUpload &desc) {
  if (!device.valid())
    return detail::invalid("Mesh upload requires a device");
  if (desc.indexFormat != IndexFormat::uint16 && desc.indexFormat != IndexFormat::uint32)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer index format");
  std::vector<specialist::MeshStream> streams;
  for (const auto &stream : desc.streams)
    streams.push_back({stream.bytes, stream.stride});
  auto raw = specialist::Mesh::upload(
      RhiInterop::native(device),
      {
          .streams = streams,
          .vertexCount = desc.vertexCount,
          .indices = desc.indices,
          .indexFormat = detail::enum_value<rhi::IndexFormat>(desc.indexFormat),
      });
  if (!raw.ok())
    return raw.status();
  Mesh result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  return result;
}
base::Result<RenderBatch> Mesh::batch(MaterialId program, std::size_t variant) const {
  if (!state_)
    return detail::invalid("Mesh is not created");
  return RhiInterop::batch(state_->raw.batch(program, variant), state_);
}
struct DepthTarget::State {
  Device device;
  specialist::DepthTarget raw;
  explicit State(Device &device)
      : device(RhiInterop::share(device)), raw(RhiInterop::native(device)) {}
};
base::Result<DepthTarget> DepthTarget::create(Device &device, PixelSize extent,
                                              TextureFormat format,
                                              std::uint32_t samples) {
  if (!device.valid())
    return detail::invalid("Depth target requires a device");
  DepthTarget result;
  result.state_ = std::make_shared<State>(device);
  if (auto status = result.state_->raw.resize(
          detail::extent(extent), detail::enum_value<rhi::TextureFormat>(format),
          samples);
      !status.ok())
    return status;
  return result;
}
Texture DepthTarget::texture() const {
  return state_ ? RhiInterop::texture(*state_->raw.texture(), nullptr, state_)
                : Texture{};
}

struct StreamBuffer::State {
  Device device;
  specialist::StreamBuffer raw;
};
base::Result<StreamBuffer> StreamBuffer::create(Device &device,
                                                const StreamBufferDesc &desc) {
  if (!device.valid())
    return detail::invalid("Stream buffer requires a device");
  if (desc.usage == BufferUsage::none || (static_cast<unsigned>(desc.usage) & ~31u) != 0)
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Unknown Renderer stream usage");
  auto raw = specialist::StreamBuffer::create(
      RhiInterop::native(device),
      {
          .usage = detail::enum_value<rhi::BufferUsage>(desc.usage),
          .initialBytes = desc.initialBytes,
          .framesInFlight = desc.framesInFlight,
      });
  if (!raw.ok())
    return raw.status();
  StreamBuffer result;
  result.state_ = std::make_shared<State>();
  result.state_->device = RhiInterop::share(device);
  result.state_->raw = std::move(raw).value();
  return result;
}
base::Status StreamBuffer::begin_frame(std::uint32_t slot) {
  return state_ ? state_->raw.begin_frame(slot)
                : detail::invalid("Stream buffer is not created");
}
base::Result<BufferSlice> StreamBuffer::write(std::span<const std::byte> bytes,
                                              std::size_t alignment) {
  if (!state_)
    return detail::invalid("Stream buffer is not created");
  auto view = state_->raw.write(bytes, alignment);
  if (!view.ok())
    return view.status();
  return RhiInterop::buffer(view.value(), state_);
}

} // namespace truffle::renderer

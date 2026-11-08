#pragma once

#include "truffle/base/status.hpp"
#include "truffle/renderer/device.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace truffle::renderer {

using MaterialId = std::uint64_t;
enum class ChannelKind : std::uint32_t {
  Transform,
  LocalTransform,
  Color,
  Normal,
  TexCoord,
  ParentIndex,
  CustomFloat4
};
enum class BindingModel : std::uint32_t { Separate, Interleaved };
enum class DrawKind : std::uint32_t { Direct, Indexed, Indirect, IndirectIndexed };
struct ChannelDesc {
  ChannelKind kind = ChannelKind::Transform;
  std::uint32_t binding = 0, offset = 0, stride = 64;
  bool operator==(const ChannelDesc &) const = default;
};
struct InstanceLayout {
  std::vector<ChannelDesc> channels;
  BindingModel bindingModel = BindingModel::Separate;
  bool operator==(const InstanceLayout &) const = default;
};

// Slices retain their storage owner, but a streamed slot still must not be
// reused before its pacing wait. Borrowed specialist buffers stay caller-owned.
class BufferSlice {
public:
  BufferSlice() = default;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

class Buffer {
public:
  Buffer() = default;
  [[nodiscard]] static base::Result<Buffer>
  upload(Device &device, std::span<const std::byte> bytes, BufferUsage usage);
  [[nodiscard]] base::Result<BufferSlice> slice(std::size_t offset = 0,
                                                std::size_t bytes = 0) const;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

class ResourceBindings {
public:
  ResourceBindings() = default;
  [[nodiscard]] bool valid() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
  friend class ProgramLayout;
};

struct RenderBatch {
  static constexpr std::uint32_t kMaxBindings = 8;
  InstanceLayout layout;
  std::uint32_t instanceCount = 0, vertexCount = 3, vertexElementCount = 0;
  BufferSlice bindings[kMaxBindings];
  DrawKind kind = DrawKind::Direct;
  MaterialId material = 0;
  std::size_t variantHash = 0;
  IndexFormat indexFormat = IndexFormat::uint32;
  BufferSlice indexBuffer, indirectBuffer, uniformBuffer;
  std::vector<ResourceBindings> bindGroups;
  std::optional<PixelRect> scissor;
};

} // namespace truffle::renderer

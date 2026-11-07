#pragma once

#include "truffle/renderer/render_batch.hpp"

#include <string>

namespace truffle::renderer {

// An attachment/sampled image reference, retaining normal resource storage.
class Texture {
public:
  Texture() = default;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] PixelSize extent() const noexcept;
  [[nodiscard]] TextureFormat format() const noexcept;
  [[nodiscard]] std::uint64_t id() const noexcept;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

enum class TextureUse { sampled, color_attachment, depth_attachment };
struct TextureTransition {
  Texture texture;
  TextureUse before, after;
};
struct TextureUpload {
  PixelSize extent;
  TextureFormat format = TextureFormat::rgba8_unorm;
  std::span<const std::byte> bytes;
  std::size_t bytesPerRow = 0;
  std::string debugName = "Renderer sampled texture";
};
struct RenderTargetDesc {
  PixelSize extent;
  TextureFormat format = TextureFormat::bgra8_unorm;
  std::string debugName = "Renderer offscreen target";
};
class SampledTexture {
public:
  SampledTexture() = default;
  [[nodiscard]] static base::Result<SampledTexture> upload(Device &,
                                                           const TextureUpload &);
  [[nodiscard]] static base::Result<SampledTexture>
  render_target(Device &, const RenderTargetDesc &);
  [[nodiscard]] base::Status update(Device &, PixelOrigin, PixelSize,
                                    std::span<const std::byte>,
                                    std::size_t bytesPerRow = 0);
  [[nodiscard]] Texture texture() const;
  [[nodiscard]] TextureTransition to_attachment() const;
  [[nodiscard]] TextureTransition to_shader_read() const;

private:
  struct State;
  std::shared_ptr<State> state_;
  friend struct RhiInterop;
};

} // namespace truffle::renderer

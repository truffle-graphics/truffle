#pragma once

#include "truffle/renderer/texture.hpp"

namespace truffle::renderer {

class DepthTarget {
public:
  DepthTarget() = default;
  [[nodiscard]] static base::Result<DepthTarget>
  create(Device &, PixelSize, TextureFormat format = TextureFormat::depth32_float,
         std::uint32_t samples = 1);
  [[nodiscard]] Texture texture() const;

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace truffle::renderer

#pragma once

#include "truffle/gui/text.hpp"
#include "truffle/renderer/specialist/texture.hpp"

namespace truffle::gui::specialist {

struct TextInterop {
  [[nodiscard]] static base::Result<TextEngine> create(rhi::Device &,
                                                       const TextEngineDesc & = {});
  [[nodiscard]] static base::Result<
      std::reference_wrapper<renderer::specialist::SampledTexture>>
  atlas(TextEngine &);
};

} // namespace truffle::gui::specialist

#pragma once

#include "truffle/gui/specialist/painter.hpp"
#include "truffle/gui/surface.hpp"

namespace truffle::gui::specialist {

struct SurfaceInterop {
  [[nodiscard]] static base::Result<std::vector<renderer::specialist::RenderBatch>>
  record(Surface &, Painter &, std::uint32_t slot, rhi::Extent2D target);
};

} // namespace truffle::gui::specialist

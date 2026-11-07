#pragma once

#include "truffle/gui/rendering.hpp"
#include "truffle/gui/specialist/painter.hpp"
#include "truffle/gui/specialist/text.hpp"
#include "truffle/renderer/rhi_interop.hpp"

#include <map>

namespace truffle::gui {

struct Graphics::State {
  GraphicsDesc desc;
  renderer::Presentation presentation;
  std::unique_ptr<renderer::PipelineCache> programs;
  std::unique_ptr<renderer::Renderer> renderer;
  specialist::Painter painter;
  std::optional<TextEngine> text;
  TextureId atlas;
  std::map<std::uint64_t, renderer::SampledTexture> images;
  bool frame_open = false;
  Context *frame_context = nullptr;
  GraphicsFrameStats stats;
};

} // namespace truffle::gui

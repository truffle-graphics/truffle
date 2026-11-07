#pragma once

#include "truffle/gui/application.hpp"
#include "truffle/gui/surface.hpp"
#include "truffle/renderer/presentation.hpp"

namespace truffle::gui {

struct RenderingFrame {
  std::uint32_t slot = 0;
  PixelSize pixels;
};

// Optional Renderer-level integration. All scene and GUI passes use one device,
// cache, Renderer and frame; GUI still owns its program and streamed geometry.
class Rendering {
public:
  static constexpr renderer::MaterialId gui_program = 0x7467'7569'0000'0001ULL;
  [[nodiscard]] static base::Result<Application> create(renderer::Device &,
                                                        const ApplicationDesc &);
  [[nodiscard]] static renderer::Device &device(Application &);
  [[nodiscard]] static renderer::PipelineCache &programs(Application &);
  [[nodiscard]] static renderer::Renderer &renderer(Application &);
  [[nodiscard]] static renderer::Presentation &presentation(Application &);
  [[nodiscard]] static base::Result<RenderingFrame>
  begin_frame(Application &, Context::Clock::time_point = Context::Clock::now());
  [[nodiscard]] static base::Result<std::vector<renderer::RenderBatch>>
  record(Application &, std::uint32_t slot, PixelSize, Surface * = nullptr);
  [[nodiscard]] static base::Result<renderer::FrameGraph::NodeId>
  append_gui_pass(Application &, renderer::FrameGraph &, const RenderingFrame &,
                  renderer::RenderPassDesc = {}, bool use_window = true,
                  Surface * = nullptr);
  [[nodiscard]] static base::Status present(Application &, const renderer::FrameGraph &);
  [[nodiscard]] static base::Status cancel_frame(Application &);
  [[nodiscard]] static base::Result<TextureId>
  add_texture(Application &, renderer::SampledTexture,
              const renderer::SamplerDesc & = {});

private:
  [[nodiscard]] static base::Result<Application> initialize(renderer::Presentation,
                                                            const ApplicationDesc &);
  friend class Application;
};

} // namespace truffle::gui

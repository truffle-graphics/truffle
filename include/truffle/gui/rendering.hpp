#pragma once

#include "truffle/gui/application.hpp"
#include "truffle/gui/surface.hpp"
#include "truffle/gui/viewport.hpp"
#include "truffle/renderer/presentation.hpp"

namespace truffle::gui {

struct ViewportTargetDesc {
  renderer::TextureFormat format = renderer::TextureFormat::bgra8_unorm;
  std::uint32_t frames_in_flight =
      0; // infer Application count; Device-only defaults to two
};
struct ViewportFrame {
  ViewportMetrics metrics;
  renderer::Texture target;
  bool redraw = false;
};

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
                  Surface * = nullptr, std::vector<renderer::TextureTransition> = {});
  [[nodiscard]] static base::Status present(Application &, const renderer::FrameGraph &);
  [[nodiscard]] static base::Status cancel_frame(Application &);
  [[nodiscard]] static base::Result<TextureId>
  add_texture(Application &, renderer::SampledTexture,
              const renderer::SamplerDesc & = {});
  [[nodiscard]] static base::Result<ViewportFrame>
  prepare_viewport(Application &, Viewport &, std::uint32_t slot,
                   const ViewportTargetDesc & = {});
  [[nodiscard]] static base::Result<ViewportFrame>
  prepare_viewport(renderer::Device &, Viewport &, std::uint32_t slot,
                   const ViewportTargetDesc & = {});
  [[nodiscard]] static base::Result<renderer::FrameGraph::NodeId>
  append_viewport_pass(Viewport &, renderer::FrameGraph &,
                       std::vector<renderer::RenderBatch>, renderer::RenderPassDesc = {});
  [[nodiscard]] static base::Status display_viewport(Application &, Viewport &);
  [[nodiscard]] static base::Status
  composite_viewport(Viewport &, renderer::FrameGraph &,
                     renderer::FrameGraph::NodeId gui_pass);
  [[nodiscard]] static base::Status finish_viewport(Viewport &, bool submitted);
  [[nodiscard]] static base::Status release_viewport(Viewport &);

private:
  [[nodiscard]] static base::Result<Application> initialize(renderer::Presentation,
                                                            const ApplicationDesc &);
  friend class Application;
};

} // namespace truffle::gui

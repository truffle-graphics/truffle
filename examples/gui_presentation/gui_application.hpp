#pragma once

#include "truffle/gui/painter.hpp"
#include "truffle/renderer/presentation.hpp"

#include <memory>

namespace truffle::examples {

// The embedding host supplies its own HWND/CAMetalLayer, input and event loop.
// This example contains no backend headers, RHI setup calls or host framework.
class GuiApplication {
public:
  [[nodiscard]] static base::Result<std::unique_ptr<GuiApplication>>
  create(renderer::WindowHandle native, rhi::Extent2D pixels, float scale = 1.0F) {
    auto presentation = renderer::create_native_presentation({
        .native = native,
        .extent = pixels,
    });
    if (!presentation.ok()) {
      return presentation.status();
    }
    auto app = std::unique_ptr<GuiApplication>(
        new GuiApplication(std::move(presentation).value()));
    auto painter = gui::Painter::create(
        app->presentation_.device(), app->programs_,
        {
            .color_format = app->presentation_.desc().format,
            .frames_in_flight = app->presentation_.desc().framesInFlight,
        });
    if (!painter.ok()) {
      return painter.status();
    }
    app->painter_ = std::move(painter).value();
    if (auto status = app->resize(pixels, scale); !status.ok()) {
      return status;
    }
    return app;
  }

  [[nodiscard]] gui::Context &gui_context() noexcept { return context_; }
  [[nodiscard]] gui::Painter &gui_painter() noexcept { return painter_; }
  [[nodiscard]] renderer::Renderer &gpu_renderer() noexcept { return renderer_; }
  [[nodiscard]] renderer::PipelineCache &pipeline_cache() noexcept { return programs_; }
  [[nodiscard]] renderer::Presentation &presentation() noexcept { return presentation_; }
  [[nodiscard]] base::Status resize(rhi::Extent2D pixels, float scale = 1.0F) {
    // Context validates the scale, including infinity/NaN and zero.
    auto status = context_.set_viewport({static_cast<float>(pixels.width) / scale,
                                         static_cast<float>(pixels.height) / scale},
                                        scale);
    if (!status.ok()) {
      return status;
    }
    return presentation_.resize(pixels);
  }

  [[nodiscard]] base::Status draw(gui::Context::Clock::time_point now,
                                  rhi::ClearColor clear = {0, 0, 0, 0}) {
    auto frame = presentation_.begin_frame();
    if (!frame.ok()) {
      return frame.status();
    }
    auto fail = [&](base::Status status) {
      (void)presentation_.cancel_frame();
      context_.request_frame();
      return status;
    };
    auto prepared = context_.prepare_frame(now);
    if (!prepared.status.ok()) {
      return fail(prepared.status);
    }
    auto batches = painter_.record(context_, frame.value().slot, frame.value().extent);
    if (!batches.ok()) {
      return fail(batches.status());
    }
    renderer::FrameGraph graph;
    rhi::RenderPassDesc pass;
    pass.colorAttachments.push_back({.clear = clear});
    graph.add_node(std::make_unique<renderer::RenderPassNode>(
        pass, std::move(batches).value(), true));
    auto status = presentation_.render(renderer_, graph);
    if (!status.ok()) {
      context_.request_frame();
    }
    return status;
  }

  [[nodiscard]] base::Status wait_idle() { return presentation_.wait_idle(); }

private:
  explicit GuiApplication(renderer::Presentation presentation)
      : presentation_(std::move(presentation)), programs_(presentation_.device()),
        renderer_(presentation_.device(), &programs_) {}

  renderer::Presentation presentation_; // destroyed after its GPU consumers
  renderer::PipelineCache programs_;
  renderer::Renderer renderer_;
  gui::Painter painter_; // outlives Context-owned viewport registrations
  gui::Context context_;
};

} // namespace truffle::examples

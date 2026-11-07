#pragma once

#include "truffle/gui/painter.hpp"
#include "truffle/renderer/specialist/frame_graph.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace truffle::gui {

struct ViewportMetrics {
  Rect bounds;                // logical coordinates in this element's own surface
  rhi::Extent2D pixels{0, 0}; // ceil(layout size * DPI scale), zero when not drawable
  float dpi_scale = 1.0F;     // host pixels per logical unit, not guessed monitor DPI
  bool drawable = false;
};

struct ViewportEvent {
  RoutedEvent &routed; // normal bubbling/defaults/capture semantics
  ViewportMetrics metrics;
  std::optional<Point> local; // logical position relative to the viewport
  std::optional<Point> pixels;
};

using ViewportEventHandler = std::function<void(Context &, ViewportEvent &)>;

struct ViewportTargetDesc {
  rhi::TextureFormat format = rhi::TextureFormat::bgra8_unorm;
  std::uint32_t frames_in_flight = 2;
  friend bool operator==(const ViewportTargetDesc &,
                         const ViewportTargetDesc &) = default;
};

struct ViewportFrame {
  ViewportMetrics metrics;
  renderer::specialist::SampledTexture *target = nullptr; // borrowed until finish_frame
  bool redraw = false;
};

// A checked reference to a Context-owned image/input element. Dropping a
// reference does not destroy it. The application owns scene state and supplies
// render batches (optionally depth) without any Engine or camera dependency.
// GPU owners/Painter remain alive and unmoved through the element and any
// outstanding graphs, or an explicit release_gpu(). One host thread owns calls.
class Viewport {
public:
  Viewport() = default;
  [[nodiscard]] static base::Result<Viewport> create(Context &context, ElementId parent);
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  // Last completed layout, including the element's detached Surface DPI.
  [[nodiscard]] base::Result<ViewportMetrics> metrics() const;
  [[nodiscard]] base::Status set_event_handler(ViewportEventHandler handler);
  [[nodiscard]] base::Status request_redraw();

  // Call after Context::prepare_frame. `slot` is the same safe FramePacer /
  // Presentation slot used by Painter and application streamed data.
  // Pair every successful prepare with finish_frame, even if hidden/cancelled.
  // Hidden/zero/clipped views allocate nothing. Unchanged scenes reuse their
  // last submitted target; a GUI-only repaint does not rerender the scene.
  [[nodiscard]] base::Result<ViewportFrame>
  prepare_frame(rhi::Device &device, std::uint32_t slot,
                const ViewportTargetDesc &desc = {});
  // Adds an offscreen pass with explicit shader-read -> attachment transition.
  // `pass` may specify clear/load/store/depth, but not another color target.
  [[nodiscard]] base::Result<renderer::specialist::FrameGraph::NodeId>
  append_render_pass(renderer::specialist::FrameGraph &graph,
                     std::vector<renderer::specialist::RenderBatch> batches,
                     rhi::RenderPassDesc pass = {});
  // Bind the prepared target before Painter::record. Then update_shapes once
  // for all changed viewport decorations; no style/layout rerun is needed.
  [[nodiscard]] base::Status display(Painter &painter);
  // After adding the GUI pass, order it after the app pass and transition the
  // target back to shader-readable. Required before submitting a redraw.
  [[nodiscard]] base::Status composite(renderer::specialist::FrameGraph &graph,
                                       renderer::specialist::FrameGraph::NodeId guiPass);
  // Pass RendererFrameStats::submitted, not Status::ok()/presented. Submission
  // commits scene revision even if presentation later failed.
  [[nodiscard]] base::Status finish_frame(bool submitted);
  // No outstanding graph may borrow this object's targets/bindings. Wait for
  // the host's submitted frames before teardown or switching device/Painter.
  [[nodiscard]] base::Status release_gpu();

private:
  struct State;
  explicit Viewport(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};

} // namespace truffle::gui

#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/window.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace truffle::gui {
class Rendering;

struct ViewportMetrics {
  Rect bounds;
  PixelSize pixels{0, 0}; // ceil(layout size * DPI), zero when not drawable
  float dpi_scale = 1.0F;
  bool drawable = false;
};
struct ViewportEvent {
  RoutedEvent &routed;
  ViewportMetrics metrics;
  std::optional<Point> local, pixels;
};
using ViewportEventHandler = std::function<void(Context &, ViewportEvent &)>;

// Context-owned image/input element. Scene state and rendering belong to the
// app; gui/rendering.hpp composes it without exposing raw graphics contracts.
class Viewport {
public:
  Viewport() = default;
  [[nodiscard]] static base::Result<Viewport> create(Context &, ElementId parent);
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  [[nodiscard]] base::Result<ViewportMetrics> metrics() const;
  [[nodiscard]] base::Status set_event_handler(ViewportEventHandler);
  [[nodiscard]] base::Status request_redraw();

private:
  struct State;
  std::shared_ptr<State> state_;
  friend class Rendering;
};

} // namespace truffle::gui

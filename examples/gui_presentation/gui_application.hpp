#pragma once

#include "truffle/gui/application.hpp"

namespace truffle::examples {

inline base::Result<gui::Application> create_gui(gui::WindowHandle window,
                                                 gui::PixelSize pixels,
                                                 float dpi_scale = 1.0F,
                                                 std::span<const std::byte> font = {}) {
  auto app = gui::Application::create({
      .window = window,
      .pixels = pixels,
      .dpi_scale = dpi_scale,
  });
  if (!app.ok())
    return app.status();
  auto &context = app.value().context();
  auto label = context.create(context.root());
  if (!label.ok())
    return label.status();
  if (auto status = context.set_layout_params(
          label.value(), {.width = 280.0F, .height = 64.0F, .position = {16, 16}});
      !status.ok())
    return status;
  if (!font.empty()) {
    auto loaded = app.value().load_font(font, 20);
    if (!loaded.ok())
      return loaded.status();
    if (auto status = app.value().set_text(label.value(), loaded.value(), "Truffle GUI");
        !status.ok())
      return status;
  }
  return app;
}

// The host forwards InputEvent to app.handle(), calls app.resize(pixelSize,
// dpiScale) on resize/DPI changes, and app.draw() only when app.needs_frame().
// Sleep until input or app.next_update(); wait_idle() before window teardown.

} // namespace truffle::examples

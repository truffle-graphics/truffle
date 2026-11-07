#pragma once

#include "truffle/gui/graphics.hpp"

namespace truffle::examples {

inline base::Result<gui::Graphics> create_gui(gui::WindowHandle window,
                                              gui::PixelSize pixels,
                                              float dpi_scale = 1.0F) {
  return gui::Graphics::create({
      .window = window,
      .pixels = pixels,
      .dpi_scale = dpi_scale,
  });
}

inline base::Status configure_gui(gui::Context &context, gui::Graphics &graphics,
                                  gui::PixelSize pixels, float dpi_scale,
                                  std::span<const std::byte> font = {}) {
  if (auto status = context.set_viewport(
          {static_cast<float>(pixels.width) / dpi_scale,
           static_cast<float>(pixels.height) / dpi_scale},
          dpi_scale);
      !status.ok())
    return status;
  auto label = context.create(context.root());
  if (!label.ok())
    return label.status();
  if (auto status = context.set_layout_params(
          label.value(), {.width = 280.0F, .height = 64.0F, .position = {16, 16}});
      !status.ok())
    return status;
  if (!font.empty()) {
    auto loaded = graphics.load_font(font, 20);
    if (!loaded.ok())
      return loaded.status();
    if (auto status = graphics.set_text(context, label.value(), loaded.value(),
                                        "Truffle GUI");
        !status.ok())
      return status;
  }
  return base::Status::success();
}

// Declare Graphics before Context so it outlives text sources that use its
// engine. The host forwards input to Context, reports resize through Graphics,
// and draws only when Graphics::needs_frame(Context&) is true.

} // namespace truffle::examples

#pragma once

#include "truffle/gui/rendering.hpp"

namespace truffle::examples {

inline base::Result<gui::Application> create_shared_gui(renderer::Device &device,
                                                        gui::WindowHandle window,
                                                        gui::PixelSize pixels,
                                                        float dpi_scale = 1.0F) {
  return gui::Rendering::create(device, {
                                            .window = window,
                                            .pixels = pixels,
                                            .dpi_scale = dpi_scale,
                                        });
}

// Scene work uses Rendering::device/programs/renderer and the same frame graph.
// Do not replace/invalidate Rendering::gui_program. The raw-device import, if
// needed by a specialist, lives separately in renderer/rhi_interop.hpp.

} // namespace truffle::examples

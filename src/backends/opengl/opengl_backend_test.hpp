#pragma once

// Private native-test hooks for the macOS GL backend. Not installed.

#include "truffle/rhi/types.hpp"

#include <cstdint>
#include <vector>

namespace truffle::rhi::detail {

// The last presented window back buffer, read with glReadPixels after the
// blit and before the swap. Rows are top-down RGBA8.
struct OpenGLPresentCapture {
    Extent2D extent;
    std::vector<std::uint8_t> rgba;
};

void set_opengl_present_capture_for_testing(bool enabled) noexcept;
[[nodiscard]] OpenGLPresentCapture opengl_last_present_capture_for_testing();

} // namespace truffle::rhi::detail

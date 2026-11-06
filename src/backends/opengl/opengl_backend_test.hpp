#pragma once

// Private native-test hooks for desktop GL presentation. Not installed.

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

// Windows only; a deterministic failure, never physical context-loss evidence.
void set_opengl_context_loss_for_testing(bool enabled) noexcept;

} // namespace truffle::rhi::detail

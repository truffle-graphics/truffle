#pragma once

#include "truffle/rhi/rhi.hpp"

#include <memory>

namespace truffle::rhi {

// Creates an uninitialized backend stub for OpenGL contract testing boundaries.
[[nodiscard]] std::unique_ptr<IBackend> create_opengl_backend();

} // namespace truffle::rhi

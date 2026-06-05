#pragma once

#include "truffle/rhi/rhi.hpp"

#include <memory>

namespace truffle::rhi {

// Creates an uninitialized backend stub for Direct3D contract testing boundaries.
[[nodiscard]] std::unique_ptr<IBackend> create_direct3d_backend();

} // namespace truffle::rhi

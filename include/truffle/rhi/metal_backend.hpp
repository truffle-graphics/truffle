#pragma once

#include "truffle/rhi/rhi.hpp"

#include <memory>

namespace truffle::rhi {

// Creates the Metal backend. Returns nullptr if Metal is not available on
// the current platform or device.
[[nodiscard]] std::unique_ptr<IBackend> create_metal_backend();

} // namespace truffle::rhi

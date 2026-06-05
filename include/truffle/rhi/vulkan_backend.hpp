#pragma once

#include "truffle/rhi/rhi.hpp"

#include <memory>

namespace truffle::rhi {

// Creates an uninitialized backend stub for Vulkan testing boundaries.
[[nodiscard]] std::unique_ptr<IBackend> create_vulkan_backend();

} // namespace truffle::rhi

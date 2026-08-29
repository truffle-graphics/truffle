#pragma once

#include "truffle/rhi/rhi.hpp"

namespace truffle::rhi {

[[nodiscard]] Result<Instance> create_null_instance(
    const InstanceDesc& desc = {});

} // namespace truffle::rhi

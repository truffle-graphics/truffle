#pragma once

namespace truffle::rhi::detail {

// Private native-test fault injection. This header is not installed.
void set_metal_device_loss_for_testing(bool enabled) noexcept;

} // namespace truffle::rhi::detail

#pragma once

namespace truffle::rhi::detail {

enum class MetalAcquireFault { none, out_of_date };

// Private native-test fault injection. This header is not installed.
void set_metal_device_loss_for_testing(bool enabled) noexcept;
void set_metal_acquire_fault_for_testing(MetalAcquireFault fault) noexcept;

} // namespace truffle::rhi::detail

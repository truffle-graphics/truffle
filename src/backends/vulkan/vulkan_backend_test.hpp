#pragma once

namespace truffle::rhi::detail {

enum class VulkanAcquireFault {
    none,
    timeout,
    out_of_date,
    surface_lost,
    device_lost,
    out_of_memory,
    suboptimal,
};

enum class VulkanPresentFault {
    none,
    timeout,
    out_of_date,
    surface_lost,
    device_lost,
    out_of_memory,
    suboptimal,
};

enum class VulkanValidationState {
    not_requested,
    not_installed,
    failed_to_load,
    active,
};

// Private native-test fault injection. This header is not installed.
void set_vulkan_device_loss_for_testing(bool enabled) noexcept;
void set_vulkan_acquire_fault_for_testing(VulkanAcquireFault fault) noexcept;
void set_vulkan_present_fault_for_testing(VulkanPresentFault fault) noexcept;
// Validation-layer outcome of the most recent instance creation.
[[nodiscard]] VulkanValidationState
last_vulkan_validation_state_for_testing() noexcept;

} // namespace truffle::rhi::detail

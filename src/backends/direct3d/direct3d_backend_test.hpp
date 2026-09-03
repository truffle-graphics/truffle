#pragma once

namespace truffle::rhi::detail {

enum class Direct3DAcquireFault {
  none,
  timeout,
  out_of_date,
  surface_lost,
  device_lost,
  out_of_memory,
  suboptimal,
};

enum class Direct3DPresentFault {
  none,
  timeout,
  out_of_date,
  surface_lost,
  device_lost,
  out_of_memory,
  suboptimal,
};

void set_direct3d_acquire_fault_for_testing(Direct3DAcquireFault fault) noexcept;
void set_direct3d_present_fault_for_testing(Direct3DPresentFault fault) noexcept;

} // namespace truffle::rhi::detail

# Vulkan Contract Simulator

This directory currently owns a headless CPU implementation of preliminary RHI
state and error contracts. It does not include Vulkan headers, load Vulkan,
create a Vulkan device, or use WSI. Its tests are contract fixtures and do not
constitute native Vulkan evidence.

Issue #33 replaces this path with native Vulkan implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

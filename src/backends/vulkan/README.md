# Vulkan Backend

This target exposes the RHI 1 Vulkan factory, which currently returns
`unsupported` and reports no adapter. It does not include Vulkan headers, load
Vulkan, create a Vulkan device, or use WSI. Shared logical validation belongs to
the Null backend rather than a simulated Vulkan adapter.

Issue #33 replaces this path with native Vulkan implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

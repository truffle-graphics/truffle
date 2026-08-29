# Direct3D Contract Simulator

This directory owns the Direct3D contract backend implementation used to
validate backend-neutral RHI behavior across queue, command-buffer state,
resource, surface/swapchain, upload-ring, and reflection-backed pipeline paths.

The implementation is intentionally headless and contract-focused. It does not
include D3D12/DXGI headers or create a native device, so CI exercises only the
preliminary shared contract. It is not evidence of D3D12 or WARP execution.

Issue #33 replaces this path with native D3D12 implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

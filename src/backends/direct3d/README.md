# Direct3D 12 Backend

This target exposes the RHI 1 Direct3D 12 factory, which currently returns
`unsupported` and reports no adapter. It does not include D3D12/DXGI headers or
create a native device. Shared logical validation belongs to the Null backend
rather than a simulated D3D12 adapter.

Issue #33 replaces this path with native D3D12 implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

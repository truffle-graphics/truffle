# Direct3D 12 Backend

On Windows this target uses only the Windows SDK to create the DXGI WARP
adapter, D3D12 device, direct queue, fence, allocator, and native command list.
The factory waits for that command list before exposing an adapter. The debug
layer is enabled when requested and installed.

This is a `native_smoke` matrix slice, not a complete backend. It owns committed
upload/readback/default-heap buffers, buffer views, mapping, native copies, and
arbitrary-range byte fills. A WARP test compares exact output after an unaligned
fill with the D3D12 debug layer enabled. Textures, shaders, pipelines,
synchronization, DXGI surface/swapchain, and presentation remain unsupported.
Non-Windows builds keep the explicit unavailable factory so no simulated D3D12
adapter can appear.

# Direct3D 12 Backend

On Windows this target uses only the Windows SDK to create the DXGI WARP
adapter, D3D12 device, direct queue, fence, allocator, and native command list.
The factory waits for that command list before exposing an adapter. The debug
layer is enabled when requested and installed.

This is a `native_smoke` matrix slice, not a complete backend. It owns committed
upload/readback/default-heap buffers, buffer views, mapping, native copies, and
arbitrary-range byte fills. A WARP test compares exact output after an unaligned
fill with the D3D12 debug layer enabled.

The resource slice also owns single-sample, device-local 1D, 2D, 3D, cube, mip,
and array textures for supported uncompressed formats. Texture views allocate
native SRV, UAV, RTV, or DSV descriptors according to resource usage and reject
incompatible formats or ranges. Copy submission tracks per-subresource states
and uses D3D12 placed footprints for buffer-texture copies. The WARP proof uses
256-byte rows for an 8x4 RGBA8 buffer -> texture -> texture -> buffer round trip
and compares the full readback buffer, including untouched padding.

The pipeline slice consumes either retained offline DXIL or native-override HLSL
from `ShaderPackage`; HLSL is compiled through the Windows SDK D3DCompiler.
Normalized reflection and target remaps build root signatures with descriptor
arrays, immutable samplers, dynamic buffer offsets, and root constants. Native
graphics and compute PSOs cover vertex layouts, blending, depth/stencil, MRT,
multisampled 2D attachments and render-pass resolve, direct/indexed/instanced
and indirect draws, and direct or indirect dispatch. WARP debug-layer tests
compare exact triangle, textured-binding, depth, MRT, MSAA-resolve, and
compute-to-render output. Layout mismatches and unimplemented capabilities fail
explicitly.

Compressed, host-visible, and external texture paths remain unsupported, as do
explicit copy-encoder texture clear/resolve/blit. Bindless tables, tessellation,
indirect-count execution, pipeline caches, dynamic depth bias, timeline
synchronization, DXGI surface/swapchain, and presentation are also unsupported.
Non-Windows builds keep the explicit unavailable factory so no simulated D3D12
adapter can appear.

# Vulkan Backend

This target privately compiles pinned Vulkan-Headers and volk. The factory
loads a native runtime, enables `VK_LAYER_KHRONOS_validation` when requested and
available, handles portability enumeration/subset extensions, discovers a real
graphics adapter, creates a device/queue, and submits a native command buffer
before exposing the RHI adapter.

The current Linux lane is `native_smoke`. It owns upload/readback and
device-local buffers, buffer views, and capability-checked device-local 1D,
2D, 3D, cube, array, mipmapped, compressed, and multisampled images. Compatible
linear/sRGB image views, buffer/image and image/image subresource copies,
uncompressed whole-subresource clear, color resolve, and nearest/linear color
blit execute natively. Tests compare exact buffer output, padded-row and
mip/layer/volume/compressed round trips, clear/blit output, and multisample
resolve when the software adapter exposes the requested format/sample count.
Depth and stencil aspects have explicit clear/readback coverage, and the Linux
lane requires BC1 plus four-sample resolve support rather than silently skipping
those paths.

The backend owns its Vulkan allocation boundary directly: it queries memory
requirements and memory types, allocates and binds each resource, and releases
the allocation with the resource. Host-visible textures, external sharing,
shaders, pipelines, synchronization, WSI, and presentation remain unsupported.
Other platforms remain `source_only`, including Apple until a pinned MoltenVK
group and execution lane exist. Shared logical validation belongs to Null and
is never used to simulate Vulkan behavior.

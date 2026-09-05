# Vulkan Backend

This target privately compiles pinned Vulkan-Headers and volk. The factory
loads a native runtime, enables `VK_LAYER_KHRONOS_validation` when requested and
available, handles portability enumeration/subset extensions, discovers a real
graphics adapter, creates a device/queue, and submits a native command buffer
before exposing the RHI adapter.

The current Linux lane is `native_smoke`. It owns upload/readback and
device-local buffers, buffer views, host-visible linear images, and
capability-checked 1D, 2D, 3D, cube, array, mipmapped, compressed, and
multisampled images. Compatible
linear/sRGB image views, buffer/image and image/image subresource copies,
uncompressed whole-subresource clear, color resolve, and nearest/linear color
blit execute natively. Tests compare exact buffer output, padded-row and
mip/layer/volume/compressed round trips, clear/blit output, and multisample
resolve when the software adapter exposes the requested format/sample count.
Depth and stencil aspects have explicit clear/readback coverage, and the Linux
lane requires BC1 plus four-sample resolve support rather than silently skipping
those paths.

Host texture reads and writes use queried linear-image subresource layouts,
preserve caller row/image padding, and explicitly synchronize host access with
the graphics queue. The backend owns its Vulkan allocation boundary directly:
it queries memory requirements and memory types, allocates and binds each
resource, and releases the allocation with the resource. Logical budget
reservation occurs before native allocation and rejects exhaustion
deterministically. The backend owns samplers, descriptor-set and pipeline
layouts, shader modules, compute pipelines, and one-color single-sample
graphics pipelines. Generated SPIR-V fixtures prove sampled-texture/sampler and
storage-buffer bindings, push constants, direct/indirect compute, and
direct/indexed/instanced/indirect triangle output with exact readback. Native
depth/stencil state and attachments, multiple render targets, and multisample
resolve are mapped through compatible transient render passes; Linux validation
proves exact depth pass/fail, red/green MRT, and four-sample resolved output.
Indirect-count, synchronization primitives, WSI, and presentation remain
unsupported. RHI 1 exposes no buffer-device-address contract. External sharing
remains unadvertised until platform handle types and ownership are defined.
Other platforms remain `source_only`, including Apple until a pinned MoltenVK
group and execution lane exist. Shared logical validation belongs to Null and
is never used to simulate Vulkan behavior.

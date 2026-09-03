# Vulkan Backend

This target privately compiles pinned Vulkan-Headers and volk. The factory
loads a native runtime, enables `VK_LAYER_KHRONOS_validation` when requested and
available, handles portability enumeration/subset extensions, discovers a real
graphics adapter, creates a device/queue, and submits a native command buffer
before exposing the RHI adapter.

The current Linux lane is `native_smoke`. It owns upload/readback and
device-local buffers, buffer views, selected device-local 2D textures and
same-format views, and buffer/texture copy plus byte-fill commands. Native tests
compare exact buffer output and an 8x4 RGBA8 buffer -> texture -> texture ->
buffer round trip with 64-byte padded rows. Texture clear/resolve/blit,
compressed and multisampled textures, shaders, pipelines, synchronization, WSI,
and presentation remain unsupported. Other platforms remain `source_only`,
including Apple until a pinned MoltenVK group and execution lane exist. Shared
logical validation belongs to Null and is never used to simulate Vulkan
behavior.

# Vulkan Backend

This target privately compiles pinned Vulkan-Headers and volk. The factory
loads a native runtime, enables `VK_LAYER_KHRONOS_validation` when requested and
available, handles portability enumeration/subset extensions, discovers a real
graphics adapter, creates a device/queue, and submits a native command buffer
before exposing the RHI adapter.

The current Linux lane is `native_smoke`. It exposes no resource, shader,
pipeline, synchronization, WSI, or presentation capabilities yet. Other
platforms remain `source_only`, including Apple until a pinned MoltenVK group
and execution lane exist. Shared logical validation belongs to Null and is
never used to simulate Vulkan behavior.

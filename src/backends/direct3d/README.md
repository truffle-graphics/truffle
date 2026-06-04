# Direct3D Backend

This directory owns the Direct3D contract backend implementation used to
validate backend-neutral RHI behavior across queue, command-buffer state,
resource, surface/swapchain, upload-ring, and reflection-backed pipeline paths.

The implementation is intentionally headless and contract-focused so CI can
exercise Direct3D backend semantics without requiring a platform-specific
runtime setup.

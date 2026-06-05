# OpenGL Backend

This directory owns the OpenGL contract backend implementation used to validate
backend-neutral RHI behavior across queue, command-buffer state, resource,
surface/swapchain, upload-ring, and reflection-backed pipeline paths.

The implementation is intentionally headless and contract-focused so CI can
exercise OpenGL backend semantics without platform GL loader/runtime coupling.

# OpenGL Contract Simulator

This directory owns the OpenGL contract backend implementation used to validate
backend-neutral RHI behavior across queue, command-buffer state, resource,
surface/swapchain, upload-ring, and reflection-backed pipeline paths.

The implementation is intentionally headless and contract-focused. It does not
load OpenGL or create a native context, so CI exercises only the preliminary
shared contract. It is not evidence of driver execution.

Issue #33 replaces this path with native profile-specific implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

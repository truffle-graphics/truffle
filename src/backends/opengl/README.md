# OpenGL Backend

This target exposes the RHI 1 OpenGL factory, which currently returns
`unsupported` and reports no adapter. It does not load OpenGL or create a native
context. Shared logical validation belongs to the Null backend rather than a
simulated OpenGL adapter.

Issue #33 replaces this path with native profile-specific implementation. See
`docs/rhi1/support-matrix.md` for the maturity gates.

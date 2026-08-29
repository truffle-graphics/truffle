# Metal Backend

This directory owns Truffle's native Metal RHI 1 implementation. The resource
slice allocates native buffers and selected single-layer, single-sample 2D
textures; creates native texture views; maps coherent host-visible memory; and
executes buffer, texture, and buffer-texture transfers with deterministic
readback. CI enables Metal API validation for its native proofs.

Other texture shapes and formats, external sharing, texture clear/resolve/blit,
pipelines, presentation, recovery, and broader Apple-platform evidence remain
explicitly unsupported or pending.

See `docs/rhi1/support-matrix.md` for the evidence gates.

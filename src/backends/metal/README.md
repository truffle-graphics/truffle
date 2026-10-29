# Metal Backend

This directory owns Truffle's native Metal RHI 1 implementation. The resource
slice allocates native buffers and selected single-layer, single-sample 2D
textures; creates native texture views; maps coherent host-visible memory; and
executes buffer, texture, and buffer-texture transfers with deterministic
readback. It also creates native shader/pipeline state, maps timeline
semaphores to shared events, and acquires and presents host-owned
`CAMetalLayer` drawables. CI enables Metal API validation for its native proofs.

`examples/host_workspace --metal` proves host-window integration: the host
owns a GLFW window and its `CAMetalLayer`, Truffle receives only a
`cocoa_layer` surface, and the bounded `--frames N` mode presents across a
scripted window resize (ctest `truffle_host_workspace_metal_window`).

Other texture shapes and formats, external sharing, texture clear/resolve/blit,
bindless/update-after-bind, serialized pipeline caches, timestamp/occlusion
queries, indirect-count, tessellation, and broader Apple-platform evidence
remain explicitly unsupported or pending; each returns `unsupported`.

## Device loss

`MTLCommandBufferErrorDeviceRemoved` maps to `device_lost`. Physical device
removal cannot occur on Apple Silicon (the macOS 27 SDK deprecates the error
for that reason); the mapping is kept as harmless code for Intel Macs with
removable eGPUs, which are not a supported target (#171). Device-loss evidence is therefore a private,
non-installed fault-injection hook that proves status propagation and
recreate-to-recover, not a physical removal. The managed-storage flush path is
likewise kept as harmless Intel-compatible code; Apple Silicon uses shared
storage.

## Hardware coverage

| Date | Hardware | OS / SDK | Result |
|---|---|---|---|
| 2026-10-29 | Apple M3 Pro (MacBook) | macOS 27.0.1 (26A434), Xcode 27.1, MacOSX27.0 SDK | Debug, warnings-as-errors: `truffle_metal_tests`, `truffle_metal_presentation_tests`, `truffle_host_workspace_metal_window`, plus Metal-linked backend-support, RHI contract and parity tests pass with API and GPU validation; no validation messages. |

Commands used:

```bash
cmake -S . -B build/metal -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DTRUFFLE_WARNINGS_AS_ERRORS=ON -DTRUFFLE_BUILD_TESTS=ON
cmake --build build/metal --target truffle_metal_tests \
  truffle_metal_presentation_tests truffle_host_workspace_example \
  truffle_backend_support_tests truffle_rhi_contract_tests \
  truffle_rhi_parity_report
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 METAL_DEVICE_WRAPPER_TYPE=1 \
  ctest --test-dir build/metal --output-on-failure \
  -R "truffle_metal|truffle_host_workspace|truffle_backend_support|truffle_rhi_contract|truffle_rhi_parity"
```

The host-window test opens a real window and needs a logged-in GUI session.
macOS support targets Apple Silicon only; Intel Macs are not a target (#171).

See `docs/rhi1/support-matrix.md` for the evidence gates.

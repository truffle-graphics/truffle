# Metal Backend

This directory owns Truffle's native Metal RHI 1 implementation. The resource
slice allocates native buffers and single-layer 2D textures (mipmapped, or
multisampled device-local); creates native texture views; maps coherent
host-visible memory; and executes buffer, texture, and buffer-texture copies,
texture clear, multisample resolve, and nearest/linear blits with deterministic
readback. It also creates native shader/pipeline state, maps timeline
semaphores to shared events, and acquires and presents host-owned
`CAMetalLayer` drawables. CI enables Metal API validation for its native proofs.

`examples/host_workspace --metal` proves host-window integration: the host
owns a GLFW window and its `CAMetalLayer`, Truffle receives only a
`cocoa_layer` surface, and the bounded `--frames N` mode presents across a
scripted window resize (ctest `truffle_host_workspace_metal_window`).

Other texture shapes, external sharing, bindless/update-after-bind, serialized
pipeline caches, timestamp/occlusion queries, indirect-count, tessellation, and
broader Apple-platform evidence remain unsupported or pending; each returns
`unsupported`.

## Advertised capability conformance

Every capability, format, and limit the macOS adapter advertises is exercised
on hardware with readback or a native rejection check (#69).
`truffle_metal_conformance_tests` (`tests/metal_conformance_tests.cpp`) closes
the gaps left by `truffle_metal_tests` (`tests/metal_backend_tests.cpp`) and
`truffle_metal_presentation_tests`.

| Advertised | Evidence |
|---|---|
| Graphics, compute, transfer queues; `memory_budget` | `metal_backend_tests`: queue-specific submits, budget query |
| Formats `r8`, `rg8`, `rgba8`, `rgba8_srgb`, `bgra8`, `bgra8_srgb`, `rgba16f`, `rgba32f` | conformance `test_color_formats`: upload/readback round trip, render, and clear of each format |
| Formats `depth16`, `depth32f`, `depth32f_stencil8` | conformance `test_depth_formats`, `test_stencil`: per-aspect upload/readback, depth/stencil clear, depth test, stencil replace/equal |
| Mipmapped textures | conformance `test_mip_levels`: per-level upload/readback, per-level clear, cross-level blit |
| Multisample (render + resolve) | `metal_backend_tests` four-sample resolve draw; conformance `test_texture_transfers` |
| `bufferViews`, `textureViews` | `metal_backend_tests` views; conformance `test_view_reinterpretation` (`rgba8` sampled through an sRGB view) |
| `hostCoherent`, map/flush/invalidate | conformance `test_mapping` |
| `bufferCopy`, `bufferFill`, `bufferTextureCopy`, `textureCopy` | `metal_backend_tests` exact readback |
| `textureClear` | conformance `test_texture_transfers`, `test_depth_formats`, `test_mip_levels` |
| `textureResolve` | conformance `test_texture_transfers`: render-pass and transfer resolve |
| `textureBlitNearest`, `textureBlitLinear` | conformance `test_texture_transfers` (rgba8, rgba16f, rgba32f, sub-rect), `test_mip_levels` |
| `ordinaryBindGroups`, `maxBindGroups` 2, `maxBindingsPerGroup`/`maxDescriptorsPerGroup` 8 | conformance `test_bind_groups`: 8 storage bindings read back; 9th binding and group 2 rejected |
| `descriptorArrays`, `immutableSamplers` | `metal_backend_tests` sampled-array draw |
| `dynamicOffsets` | conformance `test_bind_groups`: dynamic uniform and storage offsets |
| `pushConstants`, `maxPushConstantBytes` 256 | conformance `test_push_constants`: full and partial ranges in fragment and compute; 260 bytes rejected |
| `graphics`, `compute`, `indirect` | `metal_backend_tests`: exact draw/dispatch output, direct/indexed indirect draws, indirect dispatch |
| `multipleRenderTargets`, `maxColorAttachments` 8 | conformance `test_eight_color_attachments`; 9 rejected |
| `depthStencil` | conformance `test_depth_formats`, `test_stencil` |
| Blending | conformance `test_blend` |
| `maxVertexBuffers` 16 | conformance `test_vertex_buffers`: instanced two-buffer draw, all 16 slots read; 17 rejected |
| `maxViewports` 16, scissors | conformance `test_viewports_and_scissors` |
| `maxComputeWorkgroupSize` {1024, 1024, 64}, `maxComputeInvocations` 1024 | conformance `test_compute_limits` |
| Timeline semaphores, ordered submission | `metal_backend_tests` shared-event and render-to-copy ordering |
| `presentation` | `metal_presentation_tests`, `truffle_host_workspace_metal_window` |
| Device loss | `metal_backend_tests` private fault injection (see below) |
| Not advertised: timestamp/occlusion queries | conformance `test_unsupported_queries`: query pools return `unsupported` |

Known limits of the advertised features:

- Texture clear and resolve cover whole subresources only; a partial region
  returns `unsupported`. Resolve and blit are color-only, and blits reject
  multisample textures and same-texture source/destination.
- Blits are a Truffle-owned fullscreen draw, so destinations get render-target
  usage when created with `copy_destination`.
- `depth32f_stencil8` buffer-texture copies move one aspect at a time; a
  combined-aspect copy or host write/read returns `unsupported`.
- Vertex buffers bind at the Metal buffer index equal to their slot, which
  overlaps the default bind-group indices (`group * 8 + binding`) in the vertex
  stage. Vertex shaders that use both should supply a `ShaderBindingMap`.
- Timestamp queries are not advertised. On the M3 Pro, counter sampling works
  only at encoder boundaries, empty encoders record no sample, and GPU-side
  `resolveCounters` returned zeros, so a standalone `write_timestamp` plus GPU
  `resolve_queries` cannot be implemented honestly. Occlusion queries have no
  native mapping yet. Both are tracked in #175.

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
| 2026-10-30 | Apple M3 Pro (MacBook) | macOS 27.0.1 (26A434), Xcode 27.1, MacOSX27.0 SDK | Debug, warnings-as-errors: the full ctest set (30 tests, including `truffle_metal_conformance_tests`) passes with API, shader, and GPU validation; no validation messages. |
| 2026-10-29 | Apple M3 Pro (MacBook) | macOS 27.0.1 (26A434), Xcode 27.1, MacOSX27.0 SDK | Debug, warnings-as-errors: `truffle_metal_tests`, `truffle_metal_presentation_tests`, `truffle_host_workspace_metal_window`, plus Metal-linked backend-support, RHI contract and parity tests pass with API and GPU validation; no validation messages. |

Commands used:

```bash
cmake -S . -B build/metal -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DTRUFFLE_WARNINGS_AS_ERRORS=ON -DTRUFFLE_BUILD_TESTS=ON
cmake --build build/metal --target truffle_metal_tests \
  truffle_metal_conformance_tests truffle_metal_presentation_tests truffle_host_workspace_example \
  truffle_backend_support_tests truffle_rhi_contract_tests \
  truffle_rhi_parity_report
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 METAL_DEVICE_WRAPPER_TYPE=1 \
  ctest --test-dir build/metal --output-on-failure \
  -R "truffle_metal|truffle_host_workspace|truffle_backend_support|truffle_rhi_contract|truffle_rhi_parity"
```

The host-window test opens a real window and needs a logged-in GUI session.
macOS support targets Apple Silicon only; Intel Macs are not a target (#171).

See `docs/rhi1/support-matrix.md` for the evidence gates.

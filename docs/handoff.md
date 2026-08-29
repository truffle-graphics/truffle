# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Deliver issue #33's native backend/platform matrix in evidence-gated slices.
The current branch adds shared EGL-owned GL/GLES buffers, mapping, and native
copy/fill submission; textures, pipelines, synchronization, WSI, and broader
platform work follow.

## Latest Handoff

- PR #46 is merged without closing issue #33. Vulkan buffer/texture and D3D12
  buffer transfers are on `develop`; post-merge build run `33261566114` is green
  on package, macOS, Ubuntu, and Windows.
- The merged matrix slice publishes `BackendMaturity`, `PlatformKind`,
  per-dimension evidence, and the full backend/platform table through the public
  RHI. Runtime `AdapterInfo` carries its platform and maturity separately.
- `truffle-rhi-doctor` emits the repository matrix plus live adapter probes as
  JSON. Strict mode rejects a built host row at `native_smoke` when native
  initialization fails, and verifies that Null remains validation-only and
  non-native.
- Vulkan-Headers `vulkan-sdk-1.4.350.1` and volk `1.4.350` are pinned submodules
  compiled privately into `truffle_backend_vulkan`; missing source fails with an
  exact submodule recovery command and configure never downloads it.
- Vulkan handles validation-layer discovery and portability enumeration/subset,
  creates a native instance/device/graphics queue, records and submits a native
  command buffer, and exposes an adapter only after completion. Linux is the
  only proposed `native_smoke` lane in this slice.
- Windows D3D12 uses the Windows SDK to create the DXGI WARP adapter, device,
  queue, fence, allocator, and native command list, and waits for execution
  before adapter exposure.
- Linux OpenGL and OpenGL ES use surfaceless EGL pbuffers and deterministic
  clear/readback output before adapter exposure. Their native contexts are kept
  alive for empty RHI queue smoke. Resource and pipeline capabilities remain
  empty rather than simulated.
- WebGPU and WebGL2 have explicit source targets. WebGPU remains unavailable;
  WebGL2 contains an Emscripten canvas-context path but remains `source_only`
  until browser CI runs it.
- CI is segmented into shared contracts, native smoke with validation, native
  presentation, strict doctor, and parity artifacts. Linux installs Mesa Vulkan,
  validation layers, EGL, desktop GL, and GLES prerequisites; Windows uses WARP.
- The first Ubuntu run found GNU's predefined `linux` macro colliding with the
  initial `PlatformKind` enumerator in installed consumer headers. The C++ name
  is now `linux_host` while its serialized/public display value remains
  `"linux"`.
- GitHub build run `33259668349` passes package archive, macOS, Ubuntu, and
  Windows. Ubuntu proves Vulkan plus both EGL profiles and installed consumers;
  Windows proves the D3D12 WARP path; strict doctor passes on every host.
- `feat/rhi1-vulkan-resources` makes backend resource creation instance-aware
  and binds each Vulkan buffer to the instance/device that allocated it.
  Vulkan buffers now use backend-owned `VkBuffer`/`VkDeviceMemory`, select
  device-local or host-visible memory types, and implement map, unmap, flush,
  invalidate, write, and read for upload/readback domains.
- Vulkan queue submission now records native buffer copies and byte-pattern
  fills. The Linux-only native test uploads deterministic bytes, copies through
  device-local memory, fills a subrange, reads the exact result back, and checks
  mapped invalidation. PR #44's Ubuntu validation-layer lane is the native
  evidence for this result.
- `feat/rhi1-vulkan-textures` adds optimal-tiled, device-local Vulkan 2D images,
  same-format image views, per-mip/layer native layout tracking, buffer-to-image,
  image-to-buffer, and image-to-image copies. Supported uncompressed color and
  depth formats are checked against the physical device before allocation;
  compressed formats, multisampling, non-device-local images, and presentation
  images still fail explicitly.
- PR #45's Linux texture proof uses padded 64-byte rows for an 8x4 RGBA8 image,
  copies buffer -> image -> image -> buffer, and compares the complete readback
  including untouched padding. Color image transfers are the only image-transfer
  capability evidenced in that slice.
- `feat/rhi1-d3d12-buffers` adds committed upload, readback, and default-heap
  buffers tied to the creating WARP device, including map/unmap and direct host
  read/write operations. Native submission records buffer copies with explicit
  default-heap state transitions. Byte fills use transient upload buffers so
  the public operation supports unaligned offsets and sizes rather than exposing
  a D3D12-specific restriction.
- PR #46's Windows proof uploads 67 deterministic bytes, copies through a
  default-heap buffer, overwrites an unaligned 17-byte readback subrange, and
  verifies both direct and mapped output exactly under the D3D12 debug layer.
- `feat/rhi1-egl-buffers` adds buffer ownership and mapping to the shared EGL
  implementation used by desktop OpenGL and OpenGL ES. Copies use native copy
  targets; fills use transient upload buffers, preserving arbitrary byte ranges.
  Both Linux profiles share one exact upload -> device-local -> readback test
  with an unaligned fill and mapped verification.
- PR #47's first Ubuntu run reached both native profiles and found that a
  mapped-at-creation upload flush inherited the most recent global GL target
  binding. The write path now rebinds its owning buffer before explicit flush;
  native acceptance is pending the rerun.

## Durable Decisions

- RHI 1 immediately replaces the preliminary public API. There is no public
  shim or parallel backend-facing API.
- Public objects are move-only RAII wrappers over opaque generation-checked
  handles and backend dispatch.
- Portable typed outcomes and optional backend diagnostics are separate layers.
- Resource creation and queue submission are thread-safe; command recording and
  descriptor-arena mutation are single-thread-owned.
- Ordinary bind groups, layouts, and recorded resources are immutable. Arena
  reset advances an epoch and invalidates groups allocated from older epochs.
- Shader bindings retain logical identity across targets. Native remaps never
  change reflection or application layout identity.
- Pipeline creation deterministically rejects layout/reflection, push-constant,
  specialization, attachment, and workgroup mismatches.
- Optional bindless, update-after-bind, pipeline cache, tessellation, and
  indirect-count behavior is capability-reported and fails explicitly when
  unavailable.
- Presentation is a queue operation; the host owns native windows/layers and
  application lifecycle.
- Renderer descriptor caching/admission/recovery policy remains above core RHI.
- Bundled pinned source is the default for optional dependencies; configure
  performs no network fetches. Platform SDKs remain explicit prerequisites.
- Backend/platform maturity and feature support are separate. A factory exposes
  a GPU adapter only after native initialization; target presence is not
  evidence. Null is validation-only and never a GPU backend.
- ShaderPackage serialization is canonical and versioned. Runtime compiler
  adapters remain optional and backend-local.
- The Simple RHI remains deferred.

## Validation

Current #33 matrix-slice validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci -j 8
ctest --preset ci -LE 'native-smoke|presentation|validation'  # 34/34
MTL_DEBUG_LAYER=1 ctest --preset ci -L native-smoke           # 1/1
MTL_DEBUG_LAYER=1 ctest --preset ci -L presentation           # 1/1
git diff --check

cmake -S . -B build/gcc-rhi33 -G Ninja \
      -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15 \
      -DTRUFFLE_BUILD_BACKEND_METAL=OFF \
      -DTRUFFLE_BUILD_BACKEND_VULKAN=ON \
      -DTRUFFLE_BUILD_BACKEND_DIRECT3D=ON \
      -DTRUFFLE_BUILD_BACKEND_OPENGL=ON \
      -DTRUFFLE_BUILD_BACKEND_OPENGLES=ON \
      -DTRUFFLE_BUILD_BACKEND_WEBGPU=ON \
      -DTRUFFLE_BUILD_BACKEND_WEBGL2=ON \
      -DTRUFFLE_BUILD_EXAMPLES=OFF \
      -DTRUFFLE_WARNINGS_AS_ERRORS=ON
cmake --build build/gcc-rhi33 -j 8
ctest --test-dir build/gcc-rhi33 --output-on-failure  # 30/30
```

Clang and GCC warning-as-error builds pass locally with the EGL buffer slice,
as do the 34-test segmented shared suite and Metal native smoke. GitHub run
`33261275493` passes the preceding package, macOS Metal, Ubuntu
Vulkan/EGL, and Windows D3D12/WARP with strict doctor enabled. The local sandbox
does not expose a Metal device to strict doctor, so GitHub's macOS runner is the
live-device gate. Ubuntu CI remains the compile and native acceptance gate for
the shared GL/GLES implementation. `truffle_format_check` remains unavailable
because CMake does not discover `clang-format` on this host's `PATH`;
`git diff --check` passes.

## Next Resume Steps

1. Publish the EGL buffer slice and require both Ubuntu GL profiles to pass exact
   device-local readback before merging it into `develop`.
2. Continue #33 with D3D12 and GL/GLES textures before native
   shaders/pipelines and presentation.
3. Keep WebGPU/WebGL2 and every unexecuted mobile/Apple/Vulkan platform at
   `source_only`; keep native slices at `native_smoke` until shared native
   contracts and presentation evidence exist.

## Open Risks

- Submission is currently synchronous. It proves completion but does not yet
  provide asynchronous queue throughput or full deferred destruction.
- Metal barrier mapping relies on encoder boundaries and default resource hazard
  tracking; aliased heap allocations are not implemented.
- Physical Metal device-removal evidence is not available in macOS CI; the
  private fault hook validates public loss-state behavior only.
- Vulkan buffer copies/fills and buffer-image offsets currently follow Vulkan's
  four-byte transfer alignment. The texture slice is limited to device-local,
  single-sample 2D images and color transfers; host-visible images, compressed
  copies, clears, resolves, blits, shaders, WSI, and presentation remain false
  or explicitly unsupported. EGL still proves initialization and a narrow smoke
  workload only.
- D3D12 submission remains synchronous and fill commands allocate transient
  upload resources per operation. Pooling and asynchronous retirement belong to
  a later performance slice after resource correctness is evidenced.
- Linux EGL context destruction is thread-sensitive. The synchronous matrix
  slice serializes and restores the context; asynchronous GL work needs a
  deliberate context-ownership model.
- MoltenVK, Emdawnwebgpu, VMA, D3D12MA, and GL loader/state-tracker groups are
  absent until the code that consumes them is implemented and validated.
- The companion-task GitHub credential still fails before dispatch with `401
  Bad credentials`; this is external repository automation, not engine
  validation.

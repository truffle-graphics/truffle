# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Deliver issue #33's native backend/platform matrix in evidence-gated slices.
The current branch establishes native initialization/smoke and public reporting;
resource, pipeline, synchronization, WSI, and broader platform work follows.

## Latest Handoff

- PR #42 is merged and issue #31 is closed. The RHI 1 public foundation through
  explicit synchronization and presentation is on `develop`.
- `feat/rhi1-native-matrix` publishes `BackendMaturity`, `PlatformKind`,
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

Clang and GCC warning-as-error builds pass locally, as do the segmented shared
and Metal suites. The local sandbox does not expose a Metal device to strict
doctor, so GitHub's macOS runner is the strict live-device gate. Linux
Vulkan/EGL and Windows D3D12/WARP compile and runtime results remain pending
until the branch reaches GitHub CI. `truffle_format_check` remains unavailable
because `clang-format` is not installed on this host.

## Next Resume Steps

1. Commit and push the matrix foundation, open a #33 PR without closing the
   issue, and repair Linux Vulkan/EGL or Windows WARP diagnostics until strict
   platform jobs pass.
2. Merge this bounded slice, then continue #33 with backend-owned resources and
   transfers before shaders/pipelines, synchronization, and WSI/presentation.
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
- Vulkan/D3D12/EGL currently prove native initialization and one narrow workload
  only. Their intentionally empty capabilities must not be described as
  resource, shader, synchronization, WSI, or presentation support.
- Linux EGL context destruction is thread-sensitive. The synchronous matrix
  slice serializes and restores the context; asynchronous GL work needs a
  deliberate context-ownership model.
- MoltenVK, Emdawnwebgpu, VMA, D3D12MA, and GL loader/state-tracker groups are
  absent until the code that consumes them is implemented and validated.
- The companion-task GitHub credential still fails before dispatch with `401
  Bad credentials`; this is external repository automation, not engine
  validation.

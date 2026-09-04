# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Advance #53 from generated ShaderPackage consumption through native Vulkan
bindings, pipelines, and execution while preserving capability-gated claims.

## Latest Handoff

- `feat/rhi1-vulkan-shader-modules` begins #53 with Vulkan-owned SPIR-V shader
  modules. The backend rejects non-SPIR-V, partial-word, and invalid-magic
  inputs before native creation, copies package bytes into aligned words, and
  destroys modules through the device dispatch under the context lock. The
  Linux native test consumes the deterministic vertex package produced by the
  pinned glslang fixture; the fixture is an explicit CTest dependency. Build
  `33823883160` passes package, macOS, Ubuntu, and Windows; Ubuntu creates the
  native module under Vulkan validation. Local warning-as-error compilation and
  all 37 tests also pass. This checkpoint does not advertise binding, graphics,
  or compute capability and does not close #53.
  The Vulkan backend owns the native module and depends only on RHI's existing
  ShaderPackage-selected `ShaderDesc`; no public contract changes.
- #73's first compiler route is merged by PR #143 at `0e25fd0e`. Final PR-head
  Build `33820207969` passes package, macOS, Ubuntu, and Windows. Issue #73
  remains open for its other accepted compiler routes and backend-consumer
  conformance.
- Issue #55 is closed by PR #142 and merge commit `c249c164`. Its native Build
  `33818770807` and final PR-head Build `33818960477` pass package, macOS,
  Ubuntu, and Windows, including Vulkan validation-layer negative paths.
- `feat/rhi1-shaderc-spirv-route` starts #73 with glslang `16.5.0` pinned at
  `a8d28bd082bff18ffbe80996e922b012f915cf07`. The compiler remains private to
  an explicit optional tool flag; offline and package builds perform no fetch.
  The CLI compiles GLSL/GLSL ES to generated SPIR-V variants, records the exact
  compiler version/revision and bounded diagnostics, rejects invalid source,
  and checks byte-for-byte package determinism. Initial Build `33819736596`
  passed package, macOS, and Ubuntu; Windows exposed glslang's child-scope debug
  postfix mismatch under single-config MinGW. The integration now normalizes
  only the three linked glslang artifact postfixes. Replacement Build
  `33819959268` passes package, macOS, Ubuntu, and Windows, including generated
  GLSL/GLSL ES packages, deterministic repetition, invalid-source diagnostics,
  and the existing native backend lanes. Issue #73 remains open for additional
  compiler routes and consuming-backend conformance.
- Issue #129 completed the contributor-neutral planning workflow and the full
  RHI 1 backlog conversion without changing production code.
- The public Truffle Project #5 contains 95 issues with Status, Phase,
  Workstream, Priority, Effort, and Target evidence fields. Completed #26-#32,
  #48-#51, and #129 are `Done`; #52 is `In Progress`; the remaining 82
  accepted items are visible as `Todo`, with later Phase 6/7 work also labeled
  `deferred`.
- Issue #25 structurally owns #26-#35 and #129. Phase epics #33, #34, and #35
  each structurally own 27 detailed sub-issues: #48-#74, #75-#101, and
  #102-#128 respectively.
- Every new work package records context, outcome, scope, non-goals,
  architecture ownership, public-contract effect, dependencies, objective
  acceptance criteria, and required validation/native evidence. Epic bodies
  contain grouped work-breakdown indexes.
- `docs/planning.md`, the planned-work issue form, and the pull-request template
  make durable issue/Project tracking mandatory for accepted implementation and
  deliberate deferral while preserving a public-safe, contributor-neutral
  boundary.
- Repository automation follow-ups #131 and #132 record the companion-dispatch
  credential failure and deprecated action runtimes as Program/Governance work.
  They contain no credentials or private machine details.
- Issue #48 reconciles the public status documents and backend READMEs with the
  merged Vulkan, D3D12, OpenGL, and OpenGL ES resource slices. The public support
  rows now report validation evidence truthfully and link exact source commits
  and native CI runs.
- Issue #49 implements D3D12 device-local textures, native descriptor views,
  per-subresource state transitions, and padded-row buffer/texture plus
  texture/texture copies. PR #135 Build `33759484321` passes package, macOS,
  Ubuntu, and Windows; the Windows debug-layer WARP lane proves exact padded-row
  upload, texture-to-texture copy, readback, and native view creation. The only
  failing check is the separately tracked companion-routing credential issue
  #131, which is not an engine validation gate.
- Issue #50 implements D3D12 HLSL/DXIL shader ownership, reflected root
  signatures, immutable bind groups and descriptor arrays, graphics/compute
  PSOs, render/depth attachments, MSAA resolve, draw/dispatch, and indirect
  commands. Final Build `33763713016` passes package, macOS, Ubuntu, and the
  Windows debug-layer WARP suite with exact triangle, vertex-input/blend,
  textured descriptor-array/dynamic-offset output, push-constant depth
  ordering, indexed/instanced/indirect draws, MRT, MSAA-resolve, and
  compute-to-render output plus negative capability/layout cases. The separate
  companion-routing failure remains tracked by #131 and is not an engine gate.
- Issue #50 is closed by PR #136 and merge commit `ba3d8a40`; its final receipt
  Build `33763990063` passes package, macOS, Ubuntu, and Windows.
- Issue #51 is complete. It maps RHI buffer/texture and aliasing barriers to
  native D3D12 legacy barriers, gives each timeline semaphore a native fence,
  and encodes queue waits/signals. The public query contract now records
  timestamps, scoped occlusion queries, and explicit query resolution; Null
  validates the lifecycle and D3D12 owns timestamp/occlusion heaps plus exact
  readback. The first Windows build exposed one missing read-only-depth enum
  case, then the native test correctly showed that an `undefined` old layout is
  a wildcard rather than a stale state; both test/code findings are corrected.
  Build `33766645262` passes package, macOS, Ubuntu, and Windows; its WARP lane
  proves timestamp ordering and an exact 64-sample occlusion result. Explicit
  fence timeout/retry coverage and GPU-based validation pass in final Build
  `33766961283` along with package, macOS, and Ubuntu. The full local build and
  36/36 tests also pass.
- Issue #52 implements host-owned Win32 surfaces and DXGI flip-model
  swapchains, borrowed D3D12 back buffers, timeline-connected presentation,
  aligned resize/recreation, tearing capability checks, and typed recovery
  statuses. PR #138 Build `33815232612` passes package, macOS, Ubuntu, and
  Windows; its WARP presentation lane proves exact BGRA8 swapchain readback,
  acquire/render/present, client-size drift, occluded-desktop handling,
  `ResizeBuffers`, destroyed-window detection, and device-loss propagation.
- Issue #52 is closed by PR #138 and merge commit `66db0030`; final PR-head
  Build `33815897273` passes package, macOS, Ubuntu, and Windows.
- `feat/rhi1-vulkan-resources` begins #55 by expanding Vulkan images from the
  narrow 2D slice to capability-checked 1D, 2D, 3D, cube, array, mipmapped,
  compressed, and multisampled device-local shapes with compatible views.
  Native transfer recording now covers mip/layer/volume/compressed copies,
  color/depth/stencil whole-subresource clear and readback, color resolve, and
  nearest/linear color blit. The strict local build and all 36 tests pass.
  PR #140 Build `33817112365` passes package, macOS, Ubuntu, and Windows; its
  Ubuntu validation lane requires BC1, depth/stencil, four-sample resolve, and
  linear-blit exact output rather than silently skipping them. Host-visible
  textures, external-memory decisions, and allocation accounting remain in
  #55.
- `feat/rhi1-vulkan-host-memory` continues #55 with runtime-queried linear
  images for upload/readback memory. Direct texture access follows native
  subresource row/depth pitches, preserves caller padding, flushes/invalidates
  non-coherent memory, and transitions queue work to host access. The Linux
  test requires an exact padded host-upload -> native image copy ->
  host-readback round trip and verifies logical budget release. PR #141 Build
  `33817918711` passes package, macOS, Ubuntu, and Windows, including the Linux
  validation-layer proof. External
  sharing remains deliberately unsupported because no Linux handle ownership
  contract is yet exposed by the backend.
- `feat/rhi1-vulkan-resource-audit` closes the remaining #55 acceptance gaps:
  overflow-safe host layout bounds, deterministic invalid cube-view and
  host-multisample rejection, native partial-clear rejection, budget exhaustion,
  and explicit sampler/external capability gates. Samplers remain sequenced with
  #53 because the public contract exposes them through ordinary bindings; device
  addresses are not exposed by RHI 1; external handles remain unadvertised until
  a platform ownership contract exists. Commit `0c8ec99` passes the strict local
  build and all 36 tests. PR #142 Build `33818770807` passes package, macOS,
  Ubuntu, and Windows; the Ubuntu validation lane exercises the native negative
  paths. This completes #55 without promoting Vulkan beyond `native_smoke`.

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
  binding. The merged write path rebinds its owning buffer before explicit
  flush; Build `33262121061` passes both EGL native profiles.

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

Issue #129 planning-workflow validation:

```text
GitHub hierarchy: #25 has 11 children; #33, #34, and #35 have 27
children each; all phase epics link structurally back to #25.
GitHub Project #5: 95 issues; Done 8, Todo 87.
Project metadata: no missing Status, Phase, Workstream, Priority, Effort,
or Target evidence values.
Detailed RHI/governance issue audit: #48-#129 include the required actionable
sections; automation follow-ups #131 and #132 were checked separately.
Ruby YAML parse: both issue-template YAML files pass.
cmake --preset ci
cmake --build --preset ci -j 8
ctest --preset ci --output-on-failure                       # 36/36
MTL_DEBUG_LAYER=1 ctest --preset ci -L 'native-smoke|presentation'
                                                               # 2/2
git diff --check
```

Issue #48 matrix-evidence reconciliation on macOS Apple Silicon:

```text
cmake --build --preset ci -j 8
ctest --preset ci --output-on-failure                       # 36/36
MTL_DEBUG_LAYER=1 ctest --preset ci -L 'native-smoke|presentation'
                                                               # 2/2
truffle-rhi-doctor --json /tmp/truffle-rhi48-doctor.json
local Markdown link targets                                  # valid
git diff --check
```

The doctor output records `validation: true` for Linux Vulkan, Windows D3D12,
Linux OpenGL, and Linux OpenGL ES while retaining `native_smoke` maturity and
false conformance/presentation dimensions.

Issue #49 local cross-platform contract validation:

```text
cmake --build --preset ci -j 8
ctest --test-dir build/ci -R truffle_direct3d_tests --output-on-failure
git diff --check
```

The local lane verifies the non-Windows unavailable contract. PR #135 Build
`33759484321` supplies the Windows SDK compilation, WARP execution, exact
256-byte-padded row readback, native texture-view creation, and debug-layer
validation receipt required for merge.

Issue #50 local cross-platform contract validation during implementation:

```text
cmake --build --preset ci --parallel 4
ctest --preset ci --output-on-failure  # 36/36
git diff --check
```

The local macOS lane compiles the shared callback/context changes and verifies
all non-Windows contracts. Final Build `33763713016` passes package, macOS,
Ubuntu, and Windows. Its WARP debug-layer lane executes the complete #50
golden-output and negative-capability suite.

The isolated worktree required the repository's pinned Vulkan submodules; the
documented `git submodule update --init vendor/vulkan-headers vendor/volk`
recovery path succeeded before the clean configure/build/test run.

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

1. Run the final documentation receipt CI, merge PR #138, and close #52 with
   Build `33815232612` as its native evidence; then begin the next dependency-
   ready #33 sub-issue from the Project.
2. Continue #33 only through its focused Project sub-issues; update issue and
   Project state whenever scope, evidence, or disposition changes.
3. Keep WebGPU/WebGL2 and every unexecuted mobile/Apple/Vulkan platform at
   `source_only`; keep native slices at `native_smoke` until shared native
   contracts and presentation evidence exist.
4. Resolve #131 and #132 independently of engine maturity claims; neither
   follow-up blocks the already-green cross-platform Build workflow.

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
  upload resources per operation. Compressed, host-visible, external, and
  copy-encoder clear/resolve/blit texture paths remain unsupported; multisampled
  2D render/depth attachments and render-pass resolve are supported. Dynamic
  depth bias, bindless tables, tessellation, indirect-count execution, and
  pipeline caches remain explicit unsupported results. Pooling and asynchronous
  retirement belong to later performance slices after correctness is evidenced.
- Linux EGL context destruction is thread-sensitive. The synchronous matrix
  slice serializes and restores the context; asynchronous GL work needs a
  deliberate context-ownership model.
- MoltenVK, Emdawnwebgpu, VMA, D3D12MA, and GL loader/state-tracker groups are
  absent until the code that consumes them is implemented and validated.
- The companion-task GitHub credential still fails before dispatch with `401
  Bad credentials`; issue #131 tracks this external repository automation, not
  engine validation. Issue #132 separately tracks migration away from
  deprecated Node.js 20 action runtimes.

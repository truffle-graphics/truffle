# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Close #60 through shared, capability-accurate OpenGL and OpenGL ES textures,
views, samplers, transfers, state isolation, and exact native output evidence.

## Latest Handoff

- `feat/rhi1-gl-extended-resources` continues #60 from the merged 2D transfer
  checkpoint. The shared EGL implementation now selects immutable targets for
  2D arrays, mip chains, 3D textures, cube/cube-array textures, and desktop GL
  multisample textures; both profiles own core depth16 and depth24/stencil8,
  while desktop GL additionally owns its core 32-bit depth variants. Uploads
  honor row and image pitch, and framebuffer attachment selects the requested
  array layer, volume slice, cube face, and color/depth/stencil aspect. Desktop
  GL exposes runtime-gated core texture views and multisample resolves; GLES
  continues to report both explicitly unsupported. Validation-enabled contexts
  install KHR_debug when the driver exposes it and route API errors through the
  existing instance callback. Shared native tests exercise separate profile
  capability snapshots, exact mip/array/3D/cube/depth paths, desktop resolve,
  GLES negative MSAA/view behavior, external boundaries, memory budgets, and a
  zero-error debug receipt. Ownership remains in the shared GL-family backend,
  dependencies remain system EGL/GL only, and the public RHI contract is
  unchanged. Local cross-platform compilation and the existing 37-test suite
  pass; Linux GL/GLES compilation and native evidence remain pending CI.
- `feat/rhi1-gl-resources` starts #60 with a shared 2D texture checkpoint for
  both Linux EGL profiles. The backend now owns immutable uncompressed color
  textures and sampler objects, rejects external and unsupported shapes
  explicitly, and implements host texture read/write plus buffer-to-texture,
  texture-to-buffer, texture copy, partial clear, and nearest/linear framebuffer
  blit through profile-common APIs. Every operation binds its own buffer,
  texture, framebuffer, and pixel-store state and restores transient pixel-
  store/framebuffer bindings before completion. The shared native test checks
  exact padded-row upload/copy/readback, exact clear pixels, linear-blit output,
  host upload/readback, sampler creation, state rebinding across resources, and
  negative view/3D/external paths independently on desktop GL and GLES. The
  public contract is unchanged; ownership remains in the shared EGL backend
  with only system EGL/GL dependencies. Initial Build `33962059122` compiled
  both profiles but exposed a capability wiring defect: the common sampler
  factory correctly existed, but the foundation rejected sampler creation
  before reaching it because ordinary binding objects were not advertised.
  Both profiles now expose the conservative logical bind-group baseline and
  bounded limits required to create sampler objects; the shared test asserts
  that prerequisite explicitly. Replacement Build `33962339263` then passed
  the sampler and every native texture operation but caught an incorrect test
  expectation at the final external-memory boundary: ordinary creation with
  the external memory domain is contractually invalid because imports must use
  `import_texture`, whereas a shareable ordinary allocation is the operation
  that reports unsupported on these profiles. The test now covers both results
  separately. Final Build `33962754752` passes package, macOS, Ubuntu, and
  Windows. Its Ubuntu native-smoke lane proves both desktop GL and GLES exact
  transfer/clear/blit/readback results and the explicit unsupported boundaries.
  Texture views, extended shapes/formats,
  depth/stencil, and multisample resolve remain inside open issue #60 for the
  next checkpoint.
- `feat/rhi1-vulkan-linux-wsi` implements the Linux Vulkan presentation path
  for #57 without adding a window framework. Hosts pass an owned XCB connection
  and window through the existing `NativeSurface`; the backend enables
  `VK_KHR_surface`, `VK_KHR_xcb_surface`, and `VK_KHR_swapchain` only when the
  loader and adapter expose them. Surface capability, exact format, FIFO mode,
  image-count, extent, usage, queue presentation support, acquire, present,
  and resize/recreation are native and fail explicitly when unsupported.
  Acquisition waits on a native fence before returning the already-complete
  RHI acquire timeline point. Presentation bridges RHI timeline waits into a
  WSI-required binary semaphore. Borrowed swapchain images retain native
  present layout tracking but never destroy swapchain-owned `VkImage` handles.
  A Linux-only XCB test creates its own tiny host window under `xvfb-run`,
  clears a BGRA swapchain image, copies it into padded-row readback, checks every
  pixel, presents it, resizes the host window, recreates the swapchain, and
  repeats the exact proof. Unsupported surface kinds are also checked. The
  internal surface callback now receives the already-existing native context;
  Metal and D3D12 are threaded through with no public API change. The local
  macOS build and all 37 configured tests pass. Build `33945857429` passes
  package, macOS, Ubuntu, and Windows. Its Ubuntu lane compiles the Linux-only
  path, runs the full native suite with Khronos validation, and then runs the
  presentation-labeled Vulkan test through `xvfb-run`; both presentation tests
  pass. Private, non-installed acquire/present fault hooks additionally verify
  that timeout, suboptimal, out-of-date, surface-loss, allocation, and device-
  loss outcomes cross the public boundary correctly; retryable presentation
  failures preserve the acquired image, while replacement statuses release it.
  Initial receipt Build `33946162926` exposed three real portability defects:
  native swapchain image counts can exceed the requested minimum, Vulkan 1.1
  instances must enable the promoted timeline extension, and never-presented
  swapchain images begin undefined. The foundation now accepts backend-native
  image indices, extension selection follows the requested instance version,
  and each image receives a one-time undefined-to-present transition after its
  first native acquire. The same log also exposed combined depth/stencil layout
  transitions using only one aspect without the separate-layout feature; such
  barriers now cover both aspects. Replacement Build `33961381232` passes
  package, macOS, Ubuntu, and Windows. The Ubuntu log contains no Vulkan
  validation errors; its native and Xvfb presentation suites pass, including
  all typed recovery cases. The support matrix and roadmap claim only the
  evidenced native XCB path.
- `fix/rhi1-vulkan-device-loss` closes the final #54 acceptance gap found after
  PR #150 merged. A private, non-installed Vulkan test hook injects submission
  device loss; the native suite proves the submitting device becomes lost,
  further work remains rejected after the hook is cleared, and a freshly
  requested device recovers. `AdapterInfo::timestampPeriodNanoseconds` now
  makes raw query ticks calibratable: Vulkan publishes native
  `timestampPeriod`, D3D12 publishes the inverse queue frequency, and Null uses
  one nanosecond per deterministic tick. Timestamp-capable adapter tests require
  a positive period. The public addition is backend-neutral and belongs to the
  RHI adapter contract; the fault hook remains Vulkan-private. The local build
  and all 37 tests pass. Build `33945096780` passes package, macOS, Ubuntu, and
  Windows; Ubuntu proves Vulkan loss/recovery under validation and Windows
  proves D3D12 timestamp-frequency compilation and runtime calibration. The
  separate companion-routing failure remains tracked by #131.
- `feat/rhi1-vulkan-sync-foundation` implements #54's native Vulkan baseline.
  Queue kind is now carried through the internal submission boundary; Vulkan
  discovers graphics, compute, and transfer families, creates one queue for
  each distinct family, and uses concurrent sharing for resources visible to
  multiple families. Explicit buffer, per-subresource texture, and aliasing
  barriers map portable stages, access masks, and layouts to Vulkan. Native
  timeline semaphores use Vulkan 1.2 or `VK_KHR_timeline_semaphore`; timestamp
  and occlusion pools reset, record, availability-wait, and resolve 64-bit
  results. The public fence remains the foundation's conservative completed-
  submission timeline because Vulkan submission waits for queue idle before
  returning. The validation-enabled native test proves transfer-to-graphics
  semaphore ordering, multi-list submission, logical ownership, exact buffer
  output, fence timeout/retry, explicit barrier classes, timestamp ordering,
  nonzero bounded occlusion, and deterministic unsupported pipeline statistics.
  The internal queue-kind parameter is threaded through the other native
  backends without changing the public API. Ownership stays in RHI foundation
  and `src/backends/vulkan`; dependencies remain the existing RHI contract and
  pinned Vulkan headers/volk. The local build and all 37 tests pass. Initial
  Build `33944445340` reached GCC and MinGW compilation and identified two
  warnings-as-errors: an enum/flag conditional needed an explicit Vulkan flag
  cast, and the Vulkan features wrapper needed an explicit zero initializer.
  Both portability fixes are applied. Replacement Build `33944555436` passes
  package, macOS, Ubuntu, and Windows; the Ubuntu validation lane proves the
  native cross-queue, barrier, timeline, fence, and query paths. The separate
  companion-routing failure remains the non-gating defect tracked by #131.
- `feat/rhi1-vulkan-graphics-closeout` completes #53's remaining acceptance
  surface. A generated fragment package reads the storage buffer written by a
  preceding compute dispatch in the same command list and produces an exact
  blue triangle; submission inserts compute-write to fragment-read visibility
  before beginning the render pass. Native tests also prove that recorded bind
  groups retain their resources across an arena reset, retired groups fail
  deterministically, and replacement groups submit exact output. Missing and
  wrong-type reflection layouts, non-identity SPIR-V remaps, update-after-bind,
  bindless tables, pipeline caches, and indirect-count all return their
  documented invalid/unsupported status without simulation. Pipeline-cache and
  bindless execution remain untested because they remain unadvertised, as #53
  requires. Ownership stays in `src/backends/vulkan`, dependencies remain RHI
  contracts plus pinned Vulkan/glslang inputs, and there is no public API
  change. The local pinned-glslang build and all 37 tests pass. Replacement
  Build `33943018863` passes package, macOS, Ubuntu, and Windows; Ubuntu Vulkan
  validation proves exact compute-to-render output, arena lifetime behavior,
  and every selected negative gate. This satisfies #53's acceptance and the PR
  closes it on merge.
  Initial Build `33942881518` reached the native lifetime case and confirmed
  the retired group is rejected by the public encoder's stale-object guard as
  `invalid_argument`, before the lower-level arena epoch check; the assertion
  now matches that documented API boundary.
- `feat/rhi1-vulkan-depth-mrt-msaa` generalizes Vulkan graphics-pipeline and
  submission render-pass compatibility from one single-sample color target to
  multiple color targets, an optional depth/stencil attachment, and explicit
  multisample resolves. Native pipeline state now maps depth comparison/write
  and stencil faces, while unsupported depth clamp, non-fill rasterization,
  tessellation, and indirect-count remain deterministic failures. A generated
  two-output fragment package drives exact red/green MRT readback; depth-clear
  cases prove pass/fail output, and a four-sample target resolves to an exact
  red pixel. Capabilities are promoted from hardware limits only with these
  paths present. Ownership remains in `src/backends/vulkan`, dependencies stay
  limited to RHI contracts and pinned Vulkan/glslang inputs, and the public API
  is unchanged. Both local warning-enabled builds pass; all 36 normal tests and
  all 37 pinned-glslang tests pass. Initial Build `33942096908` reached the
  native suite and exposed a stale single-target assertion plus a sampled-image
  layout barrier recorded inside an active render pass. Capability assertions
  now match the promoted limits, and descriptor images are transitioned at the
  render/compute encoder boundary before native pass execution. Ubuntu Vulkan
  validation in replacement Build `33942299645` proves exact depth pass/fail,
  two-target red/green output, and four-sample resolve with clean validation;
  package, macOS, Ubuntu, and Windows lanes all pass. The separate
  companion-routing failure is the known non-gating automation defect tracked
  by #131.
- `feat/rhi1-vulkan-graphics-expanded` extends #53's proven ordinary graphics
  slice with a generated sampled-texture fragment package, reflected texture
  and sampler descriptors, graphics bind-group encoding, direct indexed and
  indirect draw commands, and dispatch-indirect. The Linux-native suite uploads
  an exact green texel, samples it into the triangle target, verifies indexed
  and indirect red triangles, and reruns the storage-buffer compute fixture via
  indirect dimensions. Vulkan advertises indirect commands only after this
  native gate; indirect-count remains unsupported. Ownership and dependencies
  remain backend-local with no public API effect. The local warnings-as-errors
  build and all 36 configured host tests pass. Initial Build `33940792618`
  reached Ubuntu compilation and exposed test calls that omitted required
  public indirect-command arguments; the tests now state the exact offset,
  indexed mode, draw count, stride, and dispatch offset. Ubuntu Vulkan
  validation in replacement Build `33941005375` proves the exact texture,
  indexed/instanced/indirect draw, and indirect-compute results; package,
  macOS, Ubuntu, and Windows lanes all pass. The separate companion-routing
  failure is the known non-gating automation defect tracked by #131.
- `feat/rhi1-vulkan-graphics` implements the first bounded Vulkan graphics
  checkpoint for #53. Generated vertex and fragment SPIR-V packages create an
  owned native graphics pipeline and compatible render pass. Submission creates
  transient attachment views/framebuffers, records clear and direct draw
  commands, transitions the render target for copy, and reads back the center
  pixel exactly. The backend advertises one single-sample color target and
  direct graphics only; depth/stencil, MRT, MSAA, indirect commands, pipeline
  caches, and non-identity remaps remain false or unsupported. Ownership stays
  in `src/backends/vulkan`; it consumes only RHI contracts and the existing
  pinned Vulkan/glslang dependencies, with no public API change. The local
  warnings-as-errors build and all 36 host tests pass; Linux Vulkan validation
  is the native evidence gate before documentation or capability claims merge.
  Initial Build `33940133323` reached GCC and found enum-versus-flags
  conditional expressions rejected by `-Werror`; stage/access mask assembly
  now uses explicitly typed Vulkan flag variables. Replacement Build
  `33940219142` passes package, macOS, Ubuntu, and Windows; Ubuntu proves the
  exact red center pixel under Vulkan validation.
- `feat/rhi1-vulkan-bindings` adds the next bounded #53 checkpoint. Vulkan now
  creates native samplers, descriptor-set/pipeline layouts, compute pipelines,
  per-submission descriptor pools/sets, push constants, and direct dispatch on
  its shared graphics/compute queue. The generated GLSL ES fixture writes four
  deterministic words through a reflected storage-buffer bind group; the Linux
  lane must prove exact host readback under validation. Adapter limits drive
  the reported binding/push-constant/workgroup limits. Non-identity SPIR-V
  binding remaps, graphics pipelines, render passes, and indirect dispatch stay
  explicitly unsupported. The backend owns all native objects and consumes
  only existing RHI contracts plus pinned Vulkan dependencies; no public API
  changes. Local warning-as-error compilation and all 37 tests pass, while this
  host cannot execute the Linux-native dispatch path. Initial Build
  `33939023870` passed package, macOS, and Windows; Ubuntu reached native
  validation and exposed three integration gaps: glslang emitted SPIR-V 1.6
  for a Vulkan 1.1 device, transfer-only images lacked usage compatible with
  the already-supported native views, and an old test assertion still expected
  bind groups to be disabled. The compiler now targets Vulkan 1.1/SPIR-V 1.3,
  view-created images carry an explicitly validated compatible usage, and the
  capability assertions match the promoted compute slice. Replacement Build
  `33939221825` proves the corrected compute path and exact readback in the
  Ubuntu validation lane; package and Windows lanes also pass. Its first macOS
  attempt encountered transient package-consumer output corruption (every
  installed archive and the generated package-version file), despite the same
  commit passing that test locally. The isolated macOS rerun passed without a
  source change, so final Build `33939221825` is green across package, macOS,
  Ubuntu, and Windows.
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

1. Run final documentation receipt CI, merge PR #152, close #57 with Build
   `33945857429` as its native evidence, and move its Project item to Done.
2. Continue #33 through its next dependency-ready focused Project sub-issue;
   update issue and
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
- Vulkan submission and presentation are synchronous correctness paths; async
  throughput and deferred destruction remain later performance work. Linux WSI
  currently supports XCB only. Wayland, Windows, Android, and MoltenVK surface
  adapters remain open platform-specific work. EGL still proves only its
  currently documented resource slice and surfaceless presentation baseline.
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

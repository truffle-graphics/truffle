# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit so
status, decisions, verification, next steps, and open risks remain current as
applicable.

Do not store raw AI transcripts, secrets, personal-only notes, or machine-private
details here. Promote durable project direction into the roadmap, architecture
docs, charter, or ADRs instead of leaving it only in this working document.

## Current Focus

Low-level graphics foundation hardening before new higher-level renderer,
scene, or ECS work. Current scope is core/RHI/backend contract depth,
validation, diagnostics, and backend parity.

## Current Work Status

- **Phase 1 to 4** are marked strictly Complete. Refer to `docs/roadmap.md` for historical feature deliverables.
- **Phase 5A** — GPU Transform Hierarchy (Complete).
  - Created `TransformComputePass` and `TransformComputePassDesc` exposing a high-level shader dispatch orchestration.
  - Implemented compute capabilities on Metal backend.
- **Phase 5B** — Frame Graph Orchestration (Complete).
  - Designed and merged `FrameGraph`, `ComputePassNode`, and `RenderPassNode` abstractions in `truffle/render/frame_graph.hpp`.
  - Re-wired `Renderer::render()` to accept `FrameGraph`, dynamically interleaving backend render passes and compute passes.
  - Authored ADR 0008 validating architecture decisions surrounding the node graph.
  - All test consumers correctly use the Frame Graph injection model instead of passing raw batches.
- **Phase 5C** — Shader Reflection & Binding Direction (Complete).
  - Added `IPipelineReflection` contract and `ResourceBinding` model in `truffle/rhi`.
  - Added reflection extraction path in Metal pipelines (render + compute).
  - Added runtime validation in `Renderer` and `TransformComputePass` that
    fails fast when provided bindings do not match reflected shader buffer
    bindings.
  - Added reflection assertions in null and Metal backend tests.
  - Added explicit negative mismatch checks for both render and compute paths in
    Metal backend tests.
- **Phase 5D** — Vulkan Parity (Complete).
  - Implemented Milestone 0 compliance path:
    - Valid command buffer object with state-machine enforcement.
    - Valid queue objects for graphics/compute/transfer.
    - Valid fence object with signal and wait behavior.
  - Implemented Milestone 1 resource foundation:
    - Vulkan buffer creation with argument validation.
    - Vulkan texture and sampler creation with argument validation.
    - Vulkan upload ring implementation with per-frame allocation and advance.
  - Implemented Milestone 2 shader/pipeline contracts:
    - Vulkan shader creation with bytecode validation.
    - Vulkan graphics and compute pipeline creation with required shader checks.
  - Implemented Milestone 3 headless surface/swapchain path:
    - Vulkan surface and swapchain creation with argument validation.
    - Headless drawable acquire, resize, and schedule_present no-op path.
  - Implemented Milestone 4 parity hardening:
    - Vulkan graphics and compute pipelines now expose deterministic reflection metadata.
    - Vulkan tests now validate reflection presence and binding contract shapes.
  - Added `truffle_vulkan_tests` and enabled it behind
    `TRUFFLE_BUILD_BACKEND_VULKAN`.
- **Phase 6** — API Stabilization & Validation Completeness (Complete).
  - Added `truffle_rhi_contract_tests` to lock shared null/Vulkan status-code
    contracts for invalid-argument and invalid-state paths.
  - Contract checks now cover buffer, texture, surface, swapchain, upload ring,
    shader creation, and command buffer state behavior across both backends.
  - Expanded contract checks to include positive-path lifecycle semantics across
    both backends: swapchain acquire/resize/present flow, upload ring allocation
    + frame advance, pipeline + compute pipeline creation, and queue submit/fence
    signaling.
  - Added `truffle/core/version.hpp` API compatibility primitives and
    `truffle_api_version_tests` CI coverage.
  - Added explicit deprecation-window policy semantics and test coverage for
    symbol availability and deprecated-state detection.
  - Shared contract tests now compile/link optional backend participants with
    compile-definition gating for deterministic portability.
- **Phase 7** — Vulkan Production Confidence (Complete).
  - Vulkan reflection and lifecycle contracts are now validated as part of the
    expanded CI contract matrix.
  - Shared backend contract tests now validate reflection metadata invariants
    (stage/type validity and duplicate binding checks).
  - CI now emits backend parity matrix artifacts summarizing tracked backend
    contract/reflection test outcomes per platform run.
  - Parity matrix tracking now includes OpenGL backend test status.
- **Phase 8** — Secondary Backend Expansion (Complete).
  - Implemented `truffle_backend_opengl` with queue/command/resource/surface/
    swapchain/upload-ring/shader reflection contract semantics.
  - Added dedicated `truffle_opengl_tests` and integrated OpenGL in shared
    RHI backend contract coverage.
- **Phase 9** — Performance & Dense Workload Readiness (Complete).
  - Replaced dynamic set-based binding validation in render/compute paths with
    fixed-size array checks to reduce per-frame allocation overhead.
  - Added `truffle_performance_sanity_tests` measurable regression gate.
- **Phase 10** — Advanced Rendering Feature Set (Complete).
  - Added dependency-aware frame-graph scheduling with cycle detection and
    explicit dependency declaration API.
  - Added resource usage declarations and implicit read/write hazard ordering
    during execution-order resolution.
  - Explicit dependency edges now override implicit hazard edges for the same
    node pair to prevent false cycles when callers intentionally order passes.
  - Added `truffle_frame_graph_dependency_tests` and
    `truffle_advanced_render_flow_tests` to enforce dependency and multi-pass
    hazard-safe execution semantics.
- **Phase 11** — Tooling, Debugging & Profiling (Complete).
  - Added `RendererFrameStats` diagnostics for compute/render node execution,
    batch count, and presentation state.
- **Phase 12** — Packaging, Distribution & Ecosystem (Complete).
  - Added baseline CPack configuration for distributable archive generation in
    install-enabled builds.
  - Added CI package automation job to build/install/package and upload archive
    artifacts.
  - Added tag-driven release workflow to publish versioned package artifacts.
  - Added `docs/distribution.md` onboarding guidance for local packaging,
    consumer verification, and release flow.
- **Direct3D Extension Track** — In Progress.
  - Added `truffle_backend_direct3d` contract backend module and public factory
    entry point (`create_direct3d_backend`).
  - Added `truffle_direct3d_tests` and optional shared contract test
    participation behind `TRUFFLE_HAS_DIRECT3D_BACKEND`.
  - Added parity report tracking entry for `truffle_direct3d_tests`.
- **Doctrine Snapshot Sync** — Complete.
  - Refreshed the repo-managed doctrine snapshot to the public-only doctrine
    baseline.
  - Added `docs/doctrine/export-policy.md` and removed
    `docs/doctrine/identity.md` from the committed snapshot.
  - Replaced the exported `AI_CONTEXT.md` with the contributor-neutral version
    and updated local repo guidance to avoid restating maintainer-private
    identity rules.
- **Low-Level Graphics Foundation Slice 1** — Complete.
  - Established `ISwapchain::schedule_present()` as a recording-time operation:
    after `end_render_pass()` and before `ICommandBuffer::end()`.
  - Added shared RHI validation helpers for non-zero extents, power-of-two
    alignment, checked alignment, and range fitting.
  - Hardened null backend into a stricter contract validator for render-pass
    lifecycle, draw/compute separation, active-pass end rejection, drawable
    invalidation on resize, and present sequencing.
  - Aligned Vulkan, OpenGL, Direct3D, and Metal present sequencing validation
    with the public RHI contract.
  - Hardened upload-ring allocation across backends for zero size, invalid
    alignment, overflow, and per-frame exhaustion.
  - Added `truffle_core_contract_tests` and expanded shared/backend tests for
    core primitives, validation helpers, upload-ring alignment, command-state
    failures, and present sequencing.
- **Low-Level Graphics Foundation Slice 2** — Complete.
  - Expanded `Capabilities` and `AdapterInfo` with adapter type, queue support,
    feature flags, device limits, format support, and memory heap topology.
  - Added RHI helper queries for queue and texture-format support.
  - Added validation helpers for extent limits and frame-count support.
  - Wired richer capability reporting through null, Metal, Vulkan, OpenGL, and
    Direct3D backends.
  - Enforced advertised buffer, texture, swapchain frame-count, surface extent,
    and upload-ring limits in backend creation paths.
  - Expanded shared backend contract tests to validate adapter metadata,
    capabilities, limits, invalid adapter IDs, format support, memory heaps, and
    limit rejection behavior.
- **Low-Level Graphics Foundation Slice 3** — Complete.
  - Added bitmask-style `BufferUsageFlags` while keeping legacy
    `BufferUsage` fallback behavior for source compatibility.
  - Added `TextureUsageFlags`, texture dimensions, depth, mip levels, array
    layers, sample count, memory domains, and buffer/texture view descriptors.
  - Added shared validation for memory-domain support, texture shape,
    resource-usage compatibility, format/usage compatibility, and view ranges.
  - Enforced buffer/texture memory, usage, shape, and format contracts across
    null, Metal, Vulkan, OpenGL, and Direct3D backends.
  - Hardened native Metal draw/dispatch paths so graphics/compute pipelines must
    be bound before native encoder calls, and indirect argument buffers are
    zero-initialized and range-checked.
  - Expanded shared backend and core contract tests for resource descriptors,
    multi-usage buffers, invalid binding usage, invalid texture shape/usage, and
    buffer/texture view validation.
- **Low-Level Graphics Foundation Slice 4A** — Complete.
  - Added public `CommandBufferState` reporting for initial, recording,
    executable, and submitted command-buffer states.
  - Documented command buffers as one-shot handles while owned by the caller;
    backend pools may recycle storage only after destruction.
  - Aligned null and Metal with Vulkan/OpenGL/Direct3D submitted-state behavior:
    a command buffer cannot be submitted twice.
  - Exposed distinct graphics, compute, and transfer queue objects for null and
    Metal to match advertised queue capabilities.
  - Expanded shared backend contract tests for command-buffer state transitions,
    duplicate submit rejection, and queue-kind routing.
- **Low-Level Graphics Foundation Slice 4B** — Complete.
  - Added explicit `ResourceState` values for undefined, copy source/destination,
    shader read, storage read/write, color/depth attachment, and present states.
  - Added buffer and texture barrier descriptors to `ICommandBuffer`.
  - Added shared validation for state/usage compatibility and barrier descriptor
    validity.
  - Wired null, Metal, Vulkan, OpenGL, and Direct3D to validate no-op contract
    barriers while recording and outside active render/compute encoders.
  - Expanded core and shared backend contract tests for valid barriers, invalid
    state/usage transitions, null barriers, and active-render-pass rejection.
- **Low-Level Graphics Foundation Slice 4C** — Complete.
  - Added `StatusCode::timeout` for explicit timeout reporting.
  - Added `IFence::wait_for(timeoutNanoseconds)` and `IFence::reset()`.
  - Wired timeout/reset behavior through null, Metal, Vulkan, OpenGL, and
    Direct3D fences.
  - Fixed Metal signal-fence reuse semantics by resetting the fence before
    attaching a command-buffer completion handler.
  - Expanded shared backend contract tests for unsignaled timeout, signaled
    zero-time wait success, and reset-to-unsignaled behavior.
- **Low-Level Graphics Foundation Slice 4D** — Complete.
  - Added timeline-style fence values with `FenceDesc::initialValue`,
    `IFence::value()`, and `IFence::wait_for_value()`.
  - Made queue signal fences advance monotonically across null, Metal, Vulkan,
    OpenGL, and Direct3D.
  - Added upload-ring frame-reuse synchronization with
    `IFrameUploadRing::advance_if_ready()` and `current_frame_index()`.
  - Expanded shared backend contract tests for timeline fence targets,
    initialized timeline fences, blocked unsignaled ring reuse, and signaled
    ring advancement.
- **Low-Level Graphics Foundation Slice 5A** — Complete.
  - Added present modes and per-swapchain image-count policy to the RHI contract.
  - Added present-mode capability reporting across null, Metal, Vulkan, OpenGL,
    and Direct3D.
  - Added swapchain image count, current image index, and acquisition-state
    queries.
  - Tightened `schedule_present()` so it requires an acquired drawable and
    consumes that acquisition on success.
  - Expanded shared/backend tests for present-mode support, image-count limit
    validation, acquire-after-resize behavior, image index reporting, and
    acquired-state transitions.
- **Low-Level Graphics Foundation Slice 5B** — Complete.
  - Added explicit native surface-kind capability reporting.
  - Added shared native surface validation for headless, Cocoa layer, Win32, XCB,
    Wayland, and external/custom handles.
  - Current contract backends advertise headless-only native surface support;
    Metal additionally advertises Cocoa layer support.
  - Added `SwapchainAcquireResult` so acquire can report status, image index,
    suboptimal state, and out-of-date state while preserving the legacy texture
    pointer wrapper.
  - Fixed the host workspace smoke path to request a headless surface when no
    native host handle is available.
- **Low-Level Graphics Foundation Slice 6** — Complete.
  - Added explicit shader byte formats and per-backend shader format capability
    reporting while preserving the legacy unknown-format path.
  - Added shader payload validation for entry points, SPIR-V magic, and
    DXIL/DXBC-style binary signatures.
  - Added explicit pipeline layout descriptors, shader-stage visibility flags,
    binding resource kinds, binding limits, and shared layout validation.
  - Added graphics render-state validation for color/depth attachment format
    support.
  - Expanded pipeline reflection metadata and added direct binding lookup across
    reflection-capable backends.
  - Added lightweight graphics/compute pipeline cache-key hooks on descriptors
    and pipeline objects.
- **Low-Level Graphics Foundation Slice 7** — Complete.
  - Added backend-neutral bind group layout and bind group descriptors with
    binding slots, visibility, resource kinds, buffer ranges, textures, and
    samplers.
  - Added shared validation for bind group layouts, entries, missing resources,
    wrong usages, duplicate bindings, minimum buffer sizes, and full-buffer range
    bindings.
  - Wired bind group layout/group creation and `ICommandBuffer::bind_group`
    through null, Metal, Vulkan, OpenGL, and Direct3D backends.
  - Added backend ownership checks so bind groups reject layouts/resources from
    other backend implementations.
  - Expanded core and shared RHI contract tests for bind group validation,
    creation failures, command binding, and mixed-backend rejection.
- **Low-Level Graphics Foundation Slice 8A** — Complete.
  - Added backend-neutral command-buffer debug labels and instantaneous debug
    markers with optional RGBA colors.
  - Added shared validation for non-empty label names and color ranges.
  - Enforced recording-time usage and balanced label scopes before
    `ICommandBuffer::end()` across null, Metal, Vulkan, OpenGL, and Direct3D.
  - Extended null backend stats with debug label and marker counters.
  - Expanded core, null, and shared RHI contract tests for valid labels,
    invalid labels, unbalanced labels, marker insertion, and label stats.
- **Low-Level Graphics Foundation Slice 8B** — Complete.
  - Added backend-neutral diagnostics counters, recent backend events, clearable
    diagnostics state, and a `collect_backend_parity_report()` helper.
  - Wired diagnostics through null, Metal, Vulkan, OpenGL, and Direct3D for
    device/resource/pipeline/bind-group/surface/swapchain/command/fence/upload
    creation, draw/dispatch/debug-marker recording, and submissions.
  - Preserved null-specific `INullBackend::stats()` compatibility by making
    `NullBackendStats` an alias of the shared `BackendStats` contract.
  - Expanded shared backend contract tests for stats growth, event ordering,
    feature parity reports, and `clear_diagnostics()` reset behavior.
- **Low-Level Graphics Foundation Slice 8C** — Complete.
  - Added explicit descriptor-array, dynamic-resource-indexing, and bindless
    resource feature gates to RHI capabilities and parity reports.
  - Added descriptor-array and bindless resource limits to device limits.
  - Added per-binding `dynamicIndexing` and `bindless` opt-in flags.
  - Hardened shared layout validation so descriptor arrays, dynamic indexing,
    and bindless layouts are rejected unless advertised by capabilities and
    within limits.
  - Expanded core and shared backend contract tests for descriptor feature-gate
    helpers, invalid layout rejection, and parity report consistency.
- **Low-Level Graphics Foundation Slice 8D** — Complete.
  - Extended the existing backend parity report generator to emit a
    machine-readable `parity-matrix.json` sidecar beside `parity-matrix.md`.
  - Updated CI and release workflows to upload the JSON parity artifact.
  - Updated distribution and architecture docs to describe Markdown + JSON parity
    artifacts.
- **Low-Level Graphics Foundation Slice 9A** — Complete.
  - Added a backend ownership hook to low-level RHI resources, shaders,
    pipelines, and bind groups.
  - Wired null, Metal, Vulkan, OpenGL, and Direct3D objects to report their
    backend kind.
  - Hardened render-pass attachments, pipeline binds, buffer binds, and
    barriers so built-in backends reject resources from another backend before
    native casts or contract execution.
  - Added shader-stage validation for graphics and compute pipeline creation
    while preserving legacy shaderless null/contract graphics pipeline
    descriptors where existing tests rely on them.
  - Expanded shared backend contract tests for foreign resource misuse and
    wrong-stage shader rejection.
- **Low-Level Graphics Foundation Slice 9B** — Complete.
  - Extended `BackendParityReport` with memory-topology fields for unified
    memory support, memory heap count, total advertised budget, and dedicated
    heap presence.
  - Wired `collect_backend_parity_report()` to derive those fields from the
    first adapter capabilities.
  - Expanded shared backend contract tests to lock parity report memory fields
    against backend capability reports.
- **Low-Level Graphics Foundation Slice 9C** — Complete.
  - Added array resource population fields to `BindGroupEntry` for buffer,
    texture, and sampler descriptor arrays while preserving scalar binding
    fields for one-element bindings.
  - Extended shared bind-group validation so array bindings require exactly the
    advertised element count, reject scalar/array mixing, and validate every
    element's usage and nullness.
  - Enabled descriptor arrays/dynamic indexing/bindless capability reporting on
    the null validation backend with bounded limits.
  - Extended backend ownership checks so all array elements must belong to the
    backend creating the bind group.
  - Expanded core and shared backend contract tests for valid descriptor arrays,
    short arrays, mixed scalar/array entries, wrong usage, and mixed-backend
    array rejection.
- **Low-Level Graphics Foundation Slice 9D** — Complete.
  - Added `truffle_rhi_parity_report`, a live backend parity report executable
    that emits `BackendParityReport` JSON for every compiled backend.
  - Added CTest coverage that writes `rhi-parity-report.json` in the build
    directory and validates the executable path.
  - Updated CI/release parity artifact uploads and distribution/architecture docs
    to include the live RHI parity JSON alongside the test-status matrix.
- **Low-Level Graphics Foundation Slice 9E** — Complete.
  - Expanded `SamplerDesc` with explicit min/mag/mipmap filters, address modes,
    LOD clamps, anisotropy, compare function, border color, and debug name while
    preserving the legacy `linear_filtering` compatibility path.
  - Added sampler descriptor retention through `ISampler::desc()` and
    backend-neutral validation for invalid enum values, LOD ranges, and
    advertised anisotropy limits.
  - Wired null, Metal, Vulkan, OpenGL, and Direct3D sampler creation to reject
    invalid descriptors, record sampler debug names in diagnostics, and report
    `maxSamplerAnisotropy` through capabilities/parity reports.
  - Mapped the richer sampler descriptor to native Metal sampler state; Metal
    explicitly rejects non-zero LOD bias until that backend can honor it.
  - Expanded core and shared backend contract tests for valid rich samplers,
    legacy nearest filtering, descriptor retention, invalid LODs, and
    anisotropy-limit rejection.
- **Low-Level Graphics Foundation Slice 9F** — Complete.
  - Added shared viewport/scissor validation for finite positive viewport
    extents, ordered normalized depth ranges, non-zero scissors, and rectangle
    overflow.
  - Aligned null, Metal, Vulkan, OpenGL, and Direct3D command buffers so
    viewport/scissor calls require an active render pass and reject invalid
    descriptors.
  - Expanded core and shared backend contract tests for invalid viewport/scissor
    descriptors and active-pass enforcement.
- **Low-Level Graphics Foundation Slice 9G** — Complete.
  - Added bind-group indices to pipeline layout bindings and a shared
    compatibility check between active pipeline layouts and bound group layouts.
  - Enforced explicit graphics/compute pipeline binding and all required
    pipeline bind groups before draw/dispatch across null, Metal, Vulkan,
    OpenGL, and Direct3D command buffers.
  - Tightened duplicate binding validation so each pipeline group and bind-group
    layout has unambiguous binding slots.
  - Updated `Renderer` to use an internal null pipeline cache when callers do
    not provide one, preserving smoke paths while satisfying the stricter RHI
    contract.
  - Expanded core, backend-specific, null, and shared RHI contract tests for
    multi-group layouts, missing bind groups, wrong group indices, and
    pipeline-required draw/dispatch behavior.
- **Low-Level Graphics Foundation Slice 9H** — Complete.
  - Added source-compatible raster, depth/stencil, and color-blend descriptors to
    `PipelineDesc`.
  - Added shared validation for fill/cull/front-face state, depth compare/stencil
    state, blend factors/ops, and color write masks.
  - Mapped Metal raster state and color blending into native pipeline/encoder
    state while leaving deeper native depth-state attachment policy for a later
    backend-specific slice.
  - Expanded core and shared backend contract tests for valid rich render state
    and invalid raster/blend/stencil descriptors.
- **Low-Level Graphics Foundation Slice 9I** — Complete.
  - Added source-compatible vertex buffer and vertex attribute layout descriptors
    to `PipelineDesc`, including step modes and typed float/uint formats.
  - Added advertised vertex-attribute and vertex-buffer stride limits plus
    shared vertex input validation for stride bounds, format size, duplicate
    bindings/locations, missing buffers, and capability overflows.
  - Extended live backend parity reports to include the new vertex input limits
    so capability JSON tracks backend differences.
  - Mapped Metal vertex input descriptors to native `MTLVertexDescriptor` state;
    contract backends retain and validate the descriptors uniformly.
  - Expanded core and shared backend contract tests for valid vertex input and
    invalid zero-stride, missing-buffer, stride-overflow, duplicate-location, and
    limit-overflow descriptors.
- **Low-Level Graphics Foundation Slice 9J** — Complete.
  - Added `TextureFormat::unknown` as an explicit no-format sentinel and
    `PipelineDesc::depthFormat` so graphics pipelines declare whether they target
    a depth attachment.
  - Made pipeline depth testing/writes opt-in by default; validation now rejects
    depth test/write state without an explicit supported depth format.
  - Added shared render-pass/pipeline compatibility validation for explicit color
    and depth attachment format matching before pipeline binding.
  - Added colorless depth-pipeline support by treating
    `PipelineDesc::colorFormat = TextureFormat::unknown` as no color target when
    a render pass contains only a depth attachment.
  - Aligned null, Metal, Vulkan, OpenGL, and Direct3D command buffers so graphics
    pipeline binding fails when the active render pass has incompatible color or
    depth attachments.
  - Mapped Metal depth attachment pixel format and depth-stencil compare/write
    state to native `MTLRenderPipelineDescriptor` and
    `MTLDepthStencilState`.
  - Expanded core and shared backend contract tests for valid depth pipelines,
    invalid implicit-depth descriptors, invalid depth formats, and depth/no-depth
    render-pass compatibility failures.
- **Low-Level Graphics Foundation Slice 9K** — Complete.
  - Added source-compatible dynamic buffer offsets to bind-group binding through
    an `ICommandBuffer::bind_group` overload that preserves the existing
    two-argument call path.
  - Added shared validation for dynamic-offset binding counts, duplicate
    binding/array-element offsets, combined static+dynamic buffer ranges, and
    advertised uniform/storage offset alignment.
  - Tightened binding-array validation so consecutive native descriptor slots
    cannot exceed `DeviceLimits::maxResourceBindings`.
  - Rejected overlapping flat native slot ranges across compatible resource
    namespaces and shader-stage visibility, including cross-group layouts that
    would alias on Metal's stage slot model.
  - Wired null, Metal, Vulkan, OpenGL, and Direct3D command buffers to enforce
    dynamic-offset contracts using their advertised limits.
  - Mapped Metal bind-group resources to native buffer, texture, and sampler
    slots across stage visibility, including descriptor-array elements and
    dynamic buffer offsets.
  - Expanded core and shared backend contract tests for valid dynamic offsets,
    missing offsets, out-of-range offsets, misaligned offsets, native-slot
    overflows, and native-slot aliasing.
- **Low-Level Graphics Foundation Slice 9L** — Complete.
  - Added source-compatible `IBuffer::map`, `IBuffer::unmap`,
    `IBuffer::mapped`, and `IBuffer::mapped_data` defaults so backends can
    expose explicit CPU buffer mapping without breaking external buffer
    implementations.
  - Added shared mappability validation: `automatic`, `upload`, and `readback`
    buffers are CPU-mappable; `device_local` buffers are not.
  - Wired null, Metal, Vulkan, OpenGL, and Direct3D buffers with mapped-at-
    creation state, double-map/double-unmap lifecycle checks, and explicit
    `device_local` map rejection.
  - Kept upload-ring `FrameAllocation::mappedPtr` coherent with
    `FrameAllocation::buffer->map()` by sharing the same backing storage in
    contract backends and validating the aliasing contract in shared tests.
  - Expanded core and shared backend contract tests for mappability, mapped-at-
    creation pointer access, remap/unmap lifecycle, and invalid mapped
    device-local buffers.
- **Low-Level Graphics Foundation Slice 9M** — Complete.
  - Added optional `BindingLayoutDesc::nativeSlot` so logical group/binding
    identities can map to explicit flattened backend-native descriptor slots.
  - Added shared effective-native-slot validation for slot ranges, descriptor
    arrays, native slot overlap checks, and pipeline-layout/bind-group
    compatibility.
  - Preserved source compatibility by defaulting the effective native slot to
    `bindingIndex` when `nativeSlot` is omitted.
  - Mapped Metal bind-group buffer/texture/sampler calls through the effective
    native slot while keeping descriptor-array element offsets intact.
  - Expanded core and shared backend contract tests for explicit native slot
    disambiguation, default native-slot alias rejection, range overflow
    rejection, compatibility checks, pipeline creation, bind-group creation, and
    command binding/draw coverage.

## Relevant Decisions And Constraints

- Truffle is embeddable graphics infrastructure, not an application host or a dedicated game engine.
- Public rendering flow stays backend-neutral while backend implementations advance behind stable contracts.
- `FrameGraph` owns high-level order, not rendering states. Individual graph nodes retain responsibility for executing `begin_render_pass` alongside backend queues.
- `truffle_render` must have zero compile-time dependency on `truffle_ecs`. `truffle_scene` is the designated ECS-to-render bridge.
- `RenderBatch` and `InstanceLayout` are the universal renderer input contract.
- Keep active handoff state curated and public-safe. Lasting decisions belong in stable docs or ADRs.
- Normal feature and fix work targets protected `develop`; stable promotion goes through `master`.
- Low-level graphics work is intentionally separated from `truffle_render`,
  `FrameGraph`, `PipelineCache`, `TransformComputePass`, `truffle_scene`, and
  `truffle_ecs`. Those higher layers may be touched only for compile fallout
  until the RHI/backend foundation is richer.
- `ISwapchain::schedule_present()` now validates command-buffer sequencing even
  for headless no-op swapchains.
- Backend diagnostics are now a backend-neutral RHI contract. Backends expose
  clearable counters, recent ordered events, and capability parity reports while
  null keeps its stricter reference-backend stats API.
- Descriptor arrays, dynamic resource indexing, and bindless resources are
  explicit feature-gated RHI capabilities. Higher layers must check the parity
  report/capability helpers before emitting indexed descriptor layouts.
- Built-in backend resources now expose `backend_kind()`; external/custom
  resource implementations default to unknown and are rejected by built-in
  backend command paths that need concrete backend ownership.
- Graphics pipeline creation accepts no shaders or a complete vertex+fragment
  pair. If shaders are supplied, they must belong to the creating backend and
  match the required stages. Contract compute backends require a compute shader;
  null preserves its shaderless compute-pipeline compatibility path.
- Backend parity reports now include memory topology summaries; higher layers
  should use those fields or the underlying capabilities rather than assuming
  UMA/discrete behavior from backend names.
- Bind groups now support explicit array resource population through
  `BindGroupEntry::buffers`, `textures`, and `samplers`; scalar fields remain
  the compatibility path for single-resource bindings.
- `parity-matrix.json` is the CI test-status matrix; `rhi-parity-report.json`
  is the live RHI capability/parity summary generated from backend contracts.
- Sampler descriptors are now explicit RHI state. Higher layers should set
  filter/address/LOD/compare fields directly and use `linear_filtering` only for
  legacy compatibility.
- Viewport and scissor state is render-pass scoped across all built-in backends;
  invalid dimensions/depth ranges are rejected before native calls.
- Draw and dispatch commands require an explicitly bound graphics/compute
  pipeline, and every bind-group index referenced by that pipeline layout must
  be bound with a compatible bind-group layout before the command records.
- Graphics pipeline descriptors now carry explicit raster, depth/stencil, and
  blend state; validation rejects unsupported stencil state and invalid enum/mask
  values before backend pipeline creation.
- Graphics pipeline descriptors now carry explicit vertex input layouts; higher
  layers should describe buffer strides, step rates, attribute locations/formats,
  and offsets directly instead of relying on renderer-owned implicit layout
  policy.
- Graphics pipeline descriptors now carry explicit depth attachment formats.
  Depth testing and writes are opt-in; a pipeline must match the active render
  pass color/depth attachment formats before it can be bound.
  Depth-only pipelines use `TextureFormat::unknown` as the color format.
- Bind-group layouts now support dynamic buffer offsets at command binding time.
  Built-in backends validate counts, ranges, array elements, and advertised
  uniform/storage alignment; layouts also reject flat native slot aliases before
  Metal maps bind-group entries into native slots.
- Buffers now expose explicit CPU mapping hooks. Built-in backends support
  mapping for automatic/upload/readback memory, reject device-local mapping, and
  track mapped-at-creation lifecycle state.
- Bind-group and pipeline-layout entries can now specify `nativeSlot` when a
  backend needs a flattened native binding index distinct from the logical
  `bindingIndex`. Omitting `nativeSlot` preserves the legacy `bindingIndex`
  mapping, so same-namespace cross-group aliases still fail unless callers
  disambiguate them explicitly.
- The repository commits only the public doctrine snapshot. The maintainer's
  private Copilot overlay lives in `~/.copilot/copilot-instructions.md` on the
  local machine and must not be copied into repository history.

## Last Verified Commands And Checks

Verified on macOS Apple Silicon (`agents/low-level-code-enhancements`):

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure   # 22/22
cmake --preset ci
cmake --build --preset ci
ctest --preset ci --output-on-failure    # 22/22
cmake -DTRUFFLE_BUILD_DIR=$PWD/build/ci -DTRUFFLE_REPORT_OUT=$PWD/build/ci/parity-matrix.md -P cmake/GenerateParityReport.cmake
python3 -m json.tool build/ci/parity-matrix.json
python3 -m json.tool build/ci/rhi-parity-report.json
```

22 tests: 3 host workspace smoke, ECS, null RHI (+ indexed draw + reflection
contract check), render flow, advanced render flow, render batch, frame graph
dependency, frame ring, scene adapter, Metal backend (+ indexed draw + compute
+ reflection checks), Vulkan milestone 0-4 tests, OpenGL backend tests,
Direct3D backend tests, shared RHI contract tests (null + Vulkan + OpenGL +
Direct3D + optional Metal), live RHI parity report, API version tests,
performance sanity tests, core contract tests, package consumer, and transform
compute tests.

## Next Resume Steps

1. Continue backend-native depth work; likely next low-level gaps are native
   depth/stencil attachment state and descriptor allocation/mapping depth.

## Open Questions Or Risks

- Advanced parity between production backends remains constrained by
  backend-specific shader compilation models.
- Vulkan, OpenGL, and Direct3D backends currently provide deterministic
  contract semantics; native API implementation and platform/runtime
  integration remain open.
- Native descriptor allocation/mapping depth remains backend-specific future
  work; the current bind group model is a validated contract layer.
- Bindless and dynamic-resource-indexing are feature-gated and descriptor arrays
  can now be populated, but backend-native bindless heap/argument-buffer mapping
  remains future work before higher layers can use bindless descriptors
  end-to-end.
- Local private Copilot overlay is configured only for this machine for now;
  any cloud/private overlay distribution model remains intentionally deferred.

## Curated Ideas Parking Lot

- Use this short list for project-relevant ideas that need another pass before they become roadmap items, ADRs, or implementation work.

## Promotion Rule

Move stable architecture, scope, roadmap, governance, or tradeoff decisions out
of this handoff when they become durable project truth. Use the charter,
architecture docs, roadmap, contributor guidance, or an ADR as appropriate.

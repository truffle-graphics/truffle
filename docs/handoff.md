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
- The repository commits only the public doctrine snapshot. The maintainer's
  private Copilot overlay lives in `~/.copilot/copilot-instructions.md` on the
  local machine and must not be copied into repository history.

## Last Verified Commands And Checks

Verified on macOS Apple Silicon (`agents/low-level-code-enhancements`):

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure   # 21/21
cmake --preset ci
cmake --build --preset ci
ctest --preset ci --output-on-failure    # 21/21
```

21 tests: 3 host workspace smoke, ECS, null RHI (+ indexed draw + reflection
contract check), render flow, advanced render flow, render batch, frame graph
dependency, frame ring, scene adapter, Metal backend (+ indexed draw + compute
+ reflection checks), Vulkan milestone 0-4 tests, OpenGL backend tests,
Direct3D backend tests, shared RHI contract tests (null + Vulkan + OpenGL +
Direct3D + optional Metal), API version tests, performance sanity tests,
core contract tests, package consumer, and transform compute tests.

## Next Resume Steps

1. Decide bindless and dynamic-resource-indexing capability flags before higher
   renderer features rely on descriptor indexing policy.
2. Add machine-readable parity report output alongside markdown/API summaries.
3. Begin backend-by-backend native depth work while preserving the shared
   diagnostics/parity contract.

## Open Questions Or Risks

- Advanced parity between production backends remains constrained by
  backend-specific shader compilation models.
- Vulkan, OpenGL, and Direct3D backends currently provide deterministic
  contract semantics; native API implementation and platform/runtime
  integration remain open.
- Native descriptor allocation/mapping depth remains backend-specific future
  work; the current bind group model is a validated contract layer.
- Bindless and dynamic-resource-indexing feature gates remain deferred; they
  should now build on the diagnostics/parity report surface rather than ad hoc
  renderer assumptions.
- Local private Copilot overlay is configured only for this machine for now;
  any cloud/private overlay distribution model remains intentionally deferred.

## Curated Ideas Parking Lot

- Use this short list for project-relevant ideas that need another pass before they become roadmap items, ADRs, or implementation work.

## Promotion Rule

Move stable architecture, scope, roadmap, governance, or tradeoff decisions out
of this handoff when they become durable project truth. Use the charter,
architecture docs, roadmap, contributor guidance, or an ADR as appropriate.

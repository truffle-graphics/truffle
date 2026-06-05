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

Post-Phase-12 stabilization with opt-in high-level asset/diagnostics layers,
parity quality gates, distribution validation, and active Direct3D
contract-backend extension work.

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
- **High-Level Asset And Diagnostics Track** — First Slice Implemented.
  - Added `truffle_assets` / `Truffle::Assets` for declarative asset, material,
    material-operation, texture, and geometry-stream metadata without file
    loading or GPU upload ownership.
  - Added backend-free material-to-mesh requirement validation so high-level
    tools can catch missing dynamic attributes before render/backend execution.
  - Added `AssetCatalog` registration and lookup for declarative mesh, material,
    texture, and geometry-stream descriptors.
  - Added catalog validation reports so tools can collect all mesh/material
    metadata issues without running loaders, uploads, or backend code.
  - Added catalog stats snapshots for grouped stream volume and residency counts
    without scanning individual detections or instances.
  - Added asset catalog groups and tags so tools can partition dense domains like
    lidar, radar, static meshes, or overlays and request group-level
    stats/validation without backend work.
  - Added `truffle_diagnostics` / `Truffle::Diagnostics` for opt-in,
    pull-based render-batch, frame-graph, and renderer-stats summaries.
  - Added external diagnostics labels for batches, frame-graph nodes, and
    resources so tool-facing names do not become permanent render-object fields.
  - Added read-only `FrameGraph` dependency/resource usage accessors and included
    those declarations in diagnostics summaries.
  - Added pull-based diagnostics budget checks that convert render-batch and
    frame-graph summaries into findings only when requested.
  - Added combined diagnostics bundles so tools can pull one report spanning
    asset catalog metadata, render batches, frame graphs, renderer stats, and
    budget findings.
  - Added group-filtered asset diagnostics so bundle reports can include all
    groups or selected groups by id/tag.
  - Added debug overlay declarations and summaries for lines, boxes, points,
    labels, and pick targets without installing renderer hooks or a persistent
    debug subsystem.
  - Added `truffle_dense_diagnostics_example`, a backend-free dense-data
    diagnostics smoke example for lidar/radar-style groups, render-batch
    summaries, and debug overlays.
  - Kept diagnostics separate from render and scene runtime dependencies; normal
    consumers only link the diagnostics target when they want inspection helpers.
  - Added `truffle_assets_tests` and `truffle_diagnostics_tests`, and extended
    the package consumer smoke check to link the new exported targets.
- **Direct3D Extension Track** — In Progress.
  - Added `truffle_backend_direct3d` contract backend module and public factory
    entry point (`create_direct3d_backend`).
  - Added `truffle_direct3d_tests` and optional shared contract test
    participation behind `TRUFFLE_HAS_DIRECT3D_BACKEND`.
  - Added parity report tracking entry for `truffle_direct3d_tests`.

## Relevant Decisions And Constraints

- Truffle is embeddable graphics infrastructure, not an application host or a dedicated game engine.
- Public rendering flow stays backend-neutral while backend implementations advance behind stable contracts.
- `FrameGraph` owns high-level order, not rendering states. Individual graph nodes retain responsibility for executing `begin_render_pass` alongside backend queues.
- `truffle_render` must have zero compile-time dependency on `truffle_ecs`. `truffle_scene` is the designated ECS-to-render bridge.
- `RenderBatch` and `InstanceLayout` are the universal renderer input contract.
- `truffle_assets` is declarative metadata only for now; file import, backend
  upload, shader compilation, and material-operation execution are explicitly
  deferred. Catalog lookup and requirement validation check declared metadata
  only; validation reports and stats snapshots collect metadata issues and
  grouped counts without side effects. Groups/tags are catalog metadata only:
  they do not imply loading, upload, or per-element iteration.
- `truffle_diagnostics` is strict opt-in tooling: no global tracing, no
  background logger, and no dependency from `truffle_render` or `truffle_scene`
  back into diagnostics. Tool-facing labels are passed as diagnostics options
  instead of stored in render objects. Graph dependency/resource usage summaries
  read existing `FrameGraph` declarations only; they do not record runtime
  events. Budget findings, group-filtered asset summaries, debug-overlay
  summaries, and bundle reports are pull-based checks, not logs or background
  profiling.
- Keep active handoff state curated and public-safe. Lasting decisions belong in stable docs or ADRs.
- Normal feature and fix work targets protected `develop`; stable promotion goes through `master`.

## Last Verified Commands And Checks

Verified on macOS Apple Silicon (`agents/high-level-abstraction-layers`):

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure   # 23/23
cmake --build --preset dev --target truffle_format_check
# clang-format was not installed; target reported the missing optional tool.
```

23 tests: 3 host workspace smoke, dense diagnostics smoke, ECS, asset
declarations, null RHI (+ indexed draw + reflection contract check), render
flow, advanced render flow, render batch, frame graph dependency, diagnostics,
frame ring, scene adapter, Metal backend (+ indexed draw + compute + reflection
checks), Vulkan milestone 0-4 tests, OpenGL backend tests, Direct3D backend
tests, shared RHI contract tests (null + Vulkan + OpenGL + Direct3D + optional
Metal), API version tests, performance sanity tests, package consumer, and
transform compute tests.

## Next Resume Steps

1. Decide whether the asset/diagnostics first slice should be promoted into a
   dedicated ADR or roadmap entry.
2. Decide whether external diagnostics labels should be joined by an optional
   GPU timing extension interface.
3. Complete Direct3D extension milestone from contract backend to
   platform-specific implementation strategy.
4. Add machine-readable parity report output alongside markdown.
5. Validate release packaging flows on additional host platforms.

## Open Questions Or Risks

- Advanced parity between production backends remains constrained by
  backend-specific shader compilation models.
- Asset/material declarations currently describe intent only; no importer,
  upload planner, or shader compiler consumes them yet.
- Diagnostics currently inspect public CPU-side contracts only; backend timing,
  overlays, and picking require future explicit extension surfaces.
- Direct3D extension currently provides contract semantics only; native API
  implementation and platform/runtime integration remain open.

## Curated Ideas Parking Lot

- Use this short list for project-relevant ideas that need another pass before they become roadmap items, ADRs, or implementation work.

## Promotion Rule

Move stable architecture, scope, roadmap, governance, or tradeoff decisions out
of this handoff when they become durable project truth. Use the charter,
architecture docs, roadmap, contributor guidance, or an ADR as appropriate.

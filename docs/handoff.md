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

Transitioning into **Phase 5**: Compute & Frame Graph Orchestration.
Specifically starting Phase 5A: GPU Transform Hierarchy compute pass (ADR 0006).

## Current Work Status

- **Phase 1 to 4** are marked strictly Complete. Refer to `docs/roadmap.md` for historical feature deliverables.
- **Phase 5A** — GPU Transform Hierarchy (in progress on `feat/phase5a-compute-hierarchy`):
  - Created `TransformComputePass` and `TransformComputePassDesc` exposing a high-level shader dispatch orchestration for updating recursive matrices in compute.
  - Implemented initial MSL kernel string `compute_transforms` in `shaders.hpp` for resolving parent matrices logic.
  - Mocked and verified the pipeline execution mapping within tests against a Null compute backend structure.
  - Integrated storage buffer bindings and active compute encoder management down into `MetalCommandBuffer` resolving pipeline validation asserts.
  - Added new `truffle_transform_compute_tests` and extended `truffle_metal_tests` to execute native MSL compute pass validating shader output mapping.



- **Phase 3C — Material system** (merged, PR #9):
  - `PipelineDesc::colorFormat` — pipelines no longer hardcode BGRA8Unorm.
  - `ShaderBinding` struct + `IPipelineCache::register_shaders()` added.
  - `PipelineCache` — hash-map cache keyed on `(InstanceLayout::hash(), MaterialId)`.
  - `RenderBatch::uniformBuffer` — per-batch constant data bound at slot
    `kMaxBindings` (8) on both VS and FS via new `ICommandBuffer::bind_uniform_buffer()`.
- **Phase 3D — Async Metal fence** (merged, PR #9):
  - `MetalFence` uses `dispatch_semaphore_t` + `shared_ptr<atomic<bool>>`; safe
    against fence outliving the GPU completion block.
  - `MetalQueue::submit()` uses async `addCompletedHandler:` instead of
    `waitUntilCompleted`.
  - `IFence` gains `wait()` for CPU synchronisation; null backend no-ops it.
- **Phase 3E — CAMetalLayer host wiring** (merged, PR #9):
  - `host_window_metal.mm` — attaches `CAMetalLayer` to GLFW window content view,
    returns as `void*` via `HostWindow::native_layer_handle()`.
  - `application.cpp` — `run_interactive_metal()` uses Metal backend with real
    MSL shaders, `cocoa_layer` surface, and `PipelineCache`; activated by `--metal`.
  - CMakeLists.txt conditionally compiles `.mm` and links `truffle_backend_metal`.
- **Phase 3F — Indexed draw** (on `feat/indexed-draw`, pending merge):
  - `rhi.hpp`: `IndexFormat` enum (uint16/uint32); `bind_index_buffer` takes
    `IndexFormat`; `ICommandBuffer` gains `draw_indexed` and
    `draw_indexed_instanced`.
  - `render_batch.hpp`: `indexFormat` field added to `RenderBatch`.
  - `metal_backend.mm`: `to_mtl_index_type` helper; `bind_index_buffer` stores
    `id<MTLBuffer>`, offset, and `MTLIndexType`; `draw_indexed_instanced` calls
    `drawIndexedPrimitives:`.
  - `null_backend.cpp`: updated signatures; `draw_indexed_instanced` increments
    `drawsRecorded`.
  - `renderer.cpp`: routes `DrawKind::Indexed` batches through
    `bind_index_buffer` + `draw_indexed_instanced`.
  - Tests: `rhi_null_tests.cpp` and `metal_backend_tests.cpp` extended with
    indexed draw coverage.

## Relevant Decisions And Constraints

- Truffle is embeddable graphics infrastructure, not an application host or a
  dedicated game engine.
- Consumers may compose Truffle with FrameKit, but Truffle must not depend on it.
- Public rendering flow stays backend-neutral; production GPU backends are later
  roadmap work.
- `truffle_render` must have zero compile-time dependency on `truffle_ecs`.
  `truffle_scene` is the designated ECS-to-render bridge (see ADR 0004).
- `RenderBatch` and `InstanceLayout` are the universal renderer input contract.
  All three data lanes (ECS extraction, bulk direct, GPU-resident) converge on
  `RenderBatch`. `InstanceLayout::hash()` keys pipeline selection.
- `IFrameUploadRing` lives at the RHI layer. It is the N-buffered CPU-to-GPU
  upload primitive for both scene extraction and bulk streaming (see ADR 0005).
- `ChannelKind::LocalTransform` and `ParentIndex` are reserved for a future
  GPU-side compute pass for hierarchy resolution (see ADR 0006). No implementation
  yet.
- `IPipelineCache` now has a real keyed implementation (`PipelineCache`);
  `NullPipelineCache` remains for null-backend-only use. Production shader
  variant selection is Phase 4 work.
- `truffle_scene` is optional. Consumers can build against `truffle_render` only
  and manage batches directly.
- Truffle does not own native window helpers in the current baseline. Host apps
  provide their own windowing and native surface boundary.
- Runtime dependencies should prefer Git submodules when practical; narrow
  pinned source copies remain the fallback when a submodule does not fit.
- The host workspace keeps a pinned, example-local GLFW source copy narrowed to
  the GLFW library build path rather than a submodule or full upstream repo
  snapshot.
- Keep active handoff state curated and public-safe. Lasting decisions belong in
  stable docs or ADRs.
- Normal feature and fix work targets protected `develop`; stable promotion goes
  through `master`.

## Last Verified Commands And Checks

Verified on 2025-05-22 (Phase 3F indexed draw, `feat/indexed-draw`):

```sh
cmake --preset dev  -DTRUFFLE_BUILD_BACKEND_METAL=ON
cmake --build --preset dev
ctest --preset dev   # 11/11
cmake --preset ci   -DTRUFFLE_BUILD_BACKEND_METAL=ON
cmake --build --preset ci  # warnings-as-errors clean
```

11 tests: 3 host workspace smoke, ECS, null RHI (+ indexed draw), render flow,
render batch, frame ring, scene adapter, Metal backend (+ indexed draw),
package consumer.

## Next Resume Steps

1. Read `AGENTS.md`, README, contributor guidance, and architecture docs.
2. Confirm `feat/indexed-draw` PR is merged; verify `develop` has Phase 3F.
3. Read ADR 0006 for GPU Transform Hierarchy context.
4. Design and implement the new Compute Pass shader execution.
4. Update this handoff before stopping on another machine.

## Open Questions Or Risks

- Workflow files now establish cross-platform CI names, but branch rules,
  required status checks, and PR-title checks still need a follow-up governance
  pass after the workflow path proves reliable.
- Truffle is not currently GitHub Project-managed. If that changes, Doctrine's
  issue-driven execution guidance becomes mandatory for planned work.

## Curated Ideas Parking Lot

- Use this short list for project-relevant ideas that need another pass before
  they become roadmap items, ADRs, or implementation work.
- Keep new entries specific enough that a later session can decide whether to
  promote or discard them.

## Promotion Rule

Move stable architecture, scope, roadmap, governance, or tradeoff decisions out
of this handoff when they become durable project truth. Use the charter,
architecture docs, roadmap, contributor guidance, or an ADR as appropriate.

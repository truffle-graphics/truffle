# Architecture

## Layers

Truffle is one product with modules that stay linkable at different levels:

1. Foundation modules such as `truffle_core` provide shared status,
   configuration, and identity primitives.
2. `truffle_rhi` defines backend-neutral devices, queues, commands, resources,
   pipelines, surfaces, swapchains, synchronization, capabilities, and the
   `IFrameUploadRing` N-buffered upload primitive.
3. `truffle_backend_*` modules implement RHI contracts behind backend ownership.
  The null, Metal, Vulkan, OpenGL, and Direct3D backends are buildable now.
4. Rendering modules such as `truffle_render` expose `RenderBatch`,
   `InstanceLayout`, `Renderer`, and the `IPipelineCache` interface. This layer
   has zero compile-time dependency on `truffle_ecs`.
5. The optional `truffle_scene` module bridges ECS worlds into render batches.
   It depends on both `truffle_ecs` and `truffle_render` and provides
   `SceneAdapter`, which extracts a `SceneFrame` containing camera state, light
   state, and a vector of `RenderBatch` objects written through
   `IFrameUploadRing`.
6. Future framework-facing modules can add assets, debug hooks, and tool-facing
   rendering workflows above the lower layers without hiding them from consumers.

## Repository Shape

Public contracts stay under `include/truffle`. Runtime implementations stay
under module-owned `src` directories, with backend code under `src/backends`
and the scene adapter under `src/scene`. CMake helpers under `cmake` own
options, warnings, formatting hooks, install rules, and package export behavior.
Examples and tests remain consumer proof and validation layers instead of
runtime dependencies.

## Three Consumption Levels

### Level 1 — Graphics API consumer
Link `truffle_core`, `truffle_rhi`, and one backend. Manage all GPU resources
directly. No ECS or scene abstraction required.

### Level 2 — Render module consumer
Add `truffle_render`. Build `RenderBatch` objects directly from any data source
(typed arrays, streaming buffers, or GPU-resident handles) and call
`Renderer::render()`. Use `IFrameUploadRing` for CPU-to-GPU uploads.

### Level 3 — Full framework consumer
Add `truffle_ecs` and `truffle_scene`. Populate an ECS world and call
`SceneAdapter::extract(world, ring)` each frame. The adapter writes transform
data through the ring and returns a `SceneFrame` with ready-to-render batches.

## Data Flow — Three Lanes

```
Lane A — ECS extraction (truffle_scene)
  ecs::World → SceneAdapter → RenderBatch[] via IFrameUploadRing
  Good for: normal scenes, cameras, lights, standard scene workflows

Lane B — Bulk direct upload (caller-owned)
  RawBuffer<T> → IFrameUploadRing allocation → RenderBatch
  Good for: million detections, point clouds, dense simulation outputs

Lane C — GPU-resident (future)
  IBuffer handle → RenderBatch (zero upload cost)
  Good for: static geometry, GPU-generated data, persistent simulation state
```

All three lanes produce `RenderBatch` objects attached to `FrameGraph`
render nodes. `Renderer::render()` executes the resolved graph order and stays
agnostic to which ingestion lane produced the batch data.

`FrameGraph` now supports explicit node dependencies and deterministic
topological scheduling with cycle detection before command recording begins,
plus resource usage declarations that inject implicit read/write hazard edges.

`InstanceLayout` declares at runtime which channels are present (`Transform`,
`Color`, `Normal`, `TexCoord`, etc.), which buffer binding each occupies, and
the per-instance stride. A `BindingModel` flag chooses Separate (SoA, default)
or Interleaved (AoS). `InstanceLayout::hash()` keys pipeline cache lookup and
future shader permutation selection.

## GPU Transform Hierarchy (Phase 5)

Large hierarchies resolve world matrices on the GPU via a compute pass over
`ChannelKind::LocalTransform` and `ChannelKind::ParentIndex` arrays.

Current state:

- `TransformComputePass` exists and dispatches through the backend-neutral RHI.
- Metal compute pipeline path is operational.
- Reflection-driven compute binding validation is in progress.

## Host Boundary

Truffle does not own native windowing, input policy, application lifetime, or
the consumer simulation model. Consumers provide host loops and native surface
boundaries, then choose whether to link low-level RHI modules, renderer modules,
scene adapter modules, or future higher-level framework modules.

## Dependency Boundary

Runtime dependencies prefer Git submodules when that is practical. Pinned
source copies are the fallback when submodules do not fit, and they must retain
license, provenance, and a narrow build footprint. Example-only dependencies
stay inside their example folders so they do not become Truffle runtime
dependencies. The `window` module boundary is reserved but unpopulated; Truffle
does not currently own windowing helpers.

## Current Baseline

The repository currently has Phases 1-12 complete for the current roadmap
scope and active extension-backend work continuing in parallel:

- `truffle_render` is ECS-independent and now supports frame-graph orchestration.
- `RenderBatch` and `InstanceLayout` remain the universal renderer input contract.
- `IFrameUploadRing` remains the N-buffered CPU-to-GPU upload primitive.
- `truffle_scene` provides optional ECS extraction via `SceneAdapter`.
- `IPipelineCache` supports material-to-pipeline mapping and variant hash routing.
- `IPipelineReflection` is integrated in Metal and Vulkan contract paths.
- `IPipelineReflection` is integrated in Metal, Vulkan, and OpenGL contract paths.
- `IPipelineReflection` contract validation now includes Direct3D path coverage.
- `RendererFrameStats` provides per-frame diagnostics for compute/render node and
  batch execution, plus presentation tracking.
- `truffle/core/version.hpp` defines public API version, compatibility policy,
  and deprecation-window semantics.
- Low-level sampler descriptors expose explicit filter, address, LOD,
  anisotropy, compare, and border-color state while preserving the legacy
  `linear_filtering` compatibility path.
- RHI command buffers require an explicit graphics/compute pipeline before
  draw/dispatch, and pipeline layouts define required bind-group indices that
  must be bound with compatible group layouts before recording work.
- Graphics pipeline descriptors include explicit raster, depth/stencil, and
  color-blend state so higher layers can express render-state policy without
  inventing backend-specific side channels.
- Graphics pipeline descriptors include explicit vertex buffer and attribute
  layouts so higher layers can pass mesh/instance input contracts to RHI without
  implicit renderer-owned vertex layout policy.
- Graphics pipeline descriptors include explicit depth attachment formats; depth
  testing/writes are opt-in and command buffers validate active render-pass
  color/depth compatibility before pipeline binding. Depth-only pipelines use
  `TextureFormat::unknown` as the color format.
- Render-pass descriptors now support stencil-capable depth attachments through
  `TextureFormat::depth32_float_stencil8` plus explicit stencil load/store/clear
  fields on `DepthAttachmentDesc`. Shared RHI validation keeps color, depth, and
  depth-stencil attachment rules aligned across null, Metal, Vulkan, OpenGL, and
  Direct3D backends before native/backend-specific execution.
- Depth/stencil pipeline state now includes explicit front/back stencil compare,
  fail/depth-fail/pass operations, read/write masks, and a render-pass-scoped
  `ICommandBuffer::set_stencil_reference()` hook so higher layers can express
  stencil behavior without backend-specific side channels.
- Bind-group layouts can mark uniform/storage buffer bindings as dynamic-offset
  bindings; command binding validates supplied offsets against descriptor arrays,
  buffer ranges, and advertised alignment before work can be recorded. Layouts
  can also specify explicit native descriptor slots for flattened backend binding
  models. Bind groups expose persistent/transient-frame allocation policy and
  layout/group cache keys plus explicit reuse/update hints (`stable`,
  `update_in_place`, `rebuild`) so higher layers can choose descriptor cache,
  rewrite, arena, and recycling strategy without inventing backend-specific side
  channels. Layouts and groups now also expose descriptor footprint summaries
  (binding count, descriptor counts, dynamic-offset count, and native
  buffer/texture/sampler slot spans) so higher layers can size descriptor
  allocation/caching policies from the public RHI. Capabilities and parity
  reports now also expose each backend's native descriptor mapping model,
  allocation model, descriptor update model, and whether logical bind groups
  flatten into one native slot space, so higher layers can align descriptor
  caching strategy with the backend's actual low-level binding direction. The
  public helper `bind_group_descriptor_strategy(...)` combines bind-group
  allocation policy, reuse hint, and backend descriptor policy into an explicit
  cache-scope / rewrite / recycle classification for higher-level descriptor
  management, including whether updates resolve as direct writes,
  copy-into-allocation rewrites, or allocation rebuilds. The same strategy now
  also exposes cache-key usability, frame-slot cardinality, and recycle-frame
  lag so higher layers can size descriptor arenas and eviction windows directly
  from the public RHI contract. Backend descriptor policy now also exposes a
  native descriptor budget model, while bind-group strategy resolves that into
  concrete budget units and eviction policy (`manual`, `frame_retire`,
  `immediate`) so higher layers can budget cache/arena pressure without
  backend-private heuristics. `bind_group_descriptor_arena_plan(...)` now folds
  those signals into explicit cache-entry counts, reservation-entry counts, and
  scaled descriptor-unit totals for persistent caches and per-frame arenas.
  `bind_group_descriptor_arena_totals(...)` then aggregates multiple bind-group
  families into whole-pool totals, splitting persistent-cache, per-frame-cache,
  and uncached transient reservation pressure while flagging invalid mixed
  budget-model aggregation. Pipeline layouts can now project the same low-level
  descriptor pressure directly through `pipeline_layout_bind_group_layout(...)`,
  `pipeline_layout_descriptor_budget(...)`,
  `pipeline_layout_bind_group_arena_plan(...)`, and
  `pipeline_layout_descriptor_arena_summary(...)`, so higher layers can derive
  per-group layouts, whole-layout budget totals, and multi-group arena pressure
  from grouped pipeline layouts without staging every bind-group family by hand.
  That same low-level layer now also exposes
  `bind_group_layout_compatible(...)`,
  `bind_group_descriptor_strategy_partition_compatible(...)`,
  `bind_group_descriptor_strategy_partition_reusable(...)`,
  `bind_group_descriptor_strategy_shareable(...)`,
  `bind_group_descriptor_family_shareable(...)`, and
  `pipeline_layout_shared_descriptor_arena_summary(...)` so multiple grouped
  pipeline layouts can be merged into shared descriptor-family budgets while
  still surfacing residency or update-policy splits that require separate pool
  partitions. That shared-family layer now also emits explicit pool-class
  partitions through `bind_group_descriptor_arena_pool_class(...)` and
  `pipeline_layout_shared_descriptor_arena_partition_summary(...)`, allowing
  higher layers to materialize concrete persistent-cache, per-frame-cache, and
  uncached reservation arenas directly from the public low-level contract. Those
  partitions now also aggregate mixed cache-key and update-path metadata so
  families that need different update policies can still reuse the same
  materialized arena partition without hiding that policy variance. Family-to-
  partition residency hooks now make the remaining distinction explicit by
  reporting lifetime class, live-object scope, and whether a family only shares
  pool capacity versus aligning 1:1 with the whole partition. The shared arena
  planning surface now also has a single bundle helper that returns families,
  partitions, residencies, and reuse cohorts together so higher layers can
  consume the full low-level descriptor plan without chaining helper calls. It
  now also emits concrete arena and reuse materialization descriptors, so
  allocator-facing code can consume partition capacities and cohort-specific
  reuse policy directly from the public low-level contract. The runtime RHI
  surface now also exposes backend-neutral `IBindGroupDescriptorArena` and
  `IBindGroupDescriptorReuseMaterializer` contracts plus shared validation for
  materialized runtime descriptors. Built-in backends now materialize retained
  descriptor-arena and reuse-materializer runtime objects directly from that
  public contract, so higher layers can target one allocator-facing interface
  without inventing a parallel runtime abstraction on top of the planning data.
  Those runtime arenas now also expose slot-aware usage snapshots, explicit
  reservation/release operations, whole-slot retirement, and full clearing, so
  low-level users can account for per-frame descriptor capacity directly from
  the public RHI instead of treating materialization descriptors as static
  metadata only. Reuse materializers have also grown into stateful runtime
  policy objects: they now expose arena-compatibility checks, state snapshots,
  round-robin per-frame reservation shaping, observed reservation tracking,
  slot retirement, and clear/reset behavior, so descriptor reuse policy is no
  longer just descriptive cohort metadata. Those two runtime contracts can now
  also be composed through `BindGroupDescriptorRuntimeCoordinator`, which tracks
  coordinated reservations and drives reserve/release/retire/clear sequencing
  through one low-level helper instead of forcing higher layers to manually keep
  arena and reuse state in lockstep. The coordinator and both underlying
  runtime objects now also expose reservation snapshots so low-level callers can
  detect drift caused by out-of-band arena/materializer mutations and reconcile
  coordinated tracking explicitly instead of silently operating on stale state.
  The runtime descriptor layer now also exposes pressure/saturation guidance for
  arenas, reuse materializers, and coordinators, including reclamation hints and
  preferred next actions derived from the same public snapshots, so higher
  layers can make eviction/reuse decisions from stable low-level signals rather
  than re-deriving pressure heuristics themselves. It now also exposes concrete
  reclamation plans that rank slot-retirement and reservation-release candidates
  from those same low-level signals, so future higher layers can ask the runtime
  what to reclaim first instead of inventing separate victim-selection policy.
  Those per-coordinator signals can now also be arbitrated across multiple
  descriptor pools/partitions, so higher layers can compare competing low-level
  pressure sources and choose where to reconcile, reclaim, or throttle first
  from one stable aggregate runtime view. That aggregate layer now also exposes
  explicit admission-control plans for prospective reservations, so higher
  layers can ask whether a request should be admitted immediately, reconciled
  first, reclaimed first, audited, or throttled from one backend-neutral
  low-level decision surface. It now also supports batched quota carving across
  several reservation intents, so callers can simulate how shared low-level
  descriptor headroom and reclaimable relief would be consumed before they
  commit those requests against live coordinators. That batch layer now also has
  an execution path that can reconcile, reclaim, reserve, and roll back
  committed reservations in order, so accepted quota plans can move through one
  rollback-aware low-level commit surface instead of ad hoc caller sequencing.
  Saved batch plans can now also be revalidated against live coordinator state
  before execution, so higher layers can detect when drift, incompatibility, or
  changed slot headroom has made a previously accepted low-level plan unsafe.
  Those saved plans can now also be repaired by replaying the original request
  intents through current low-level coordinator state, which yields an explicit
  replacement plan plus per-request rewrite metadata when placements or
  admission actions must change.
- Buffers expose explicit CPU mapping hooks, including `mapped_data()` access for
  mapped-at-creation buffers. The low-level contract treats
  automatic/upload/readback buffer memory as CPU-mappable and rejects
  `device_local` mapping, with mapped state tracked by built-in backends.
- CI emits backend parity matrix Markdown/JSON artifacts for tracked backend
  contract/reflection tests, plus a live `rhi-parity-report.json` generated
  from `BackendParityReport` capability summaries.

ADRs 0004-0008 lock the current design direction; ADR 0009 governs phase
completion policy and execution gating; ADR 0010 defines API compatibility
versioning semantics.

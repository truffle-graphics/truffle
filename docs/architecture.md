# Architecture

## Layers

Truffle is one product with modules that stay linkable at different levels:

1. Foundation modules such as `truffle_core` provide shared status,
   configuration, and identity primitives.
2. `truffle_rhi` defines backend-neutral devices, queues, commands, resources,
   pipelines, surfaces, swapchains, synchronization, capabilities, and the
   `IFrameUploadRing` N-buffered upload primitive.
3. `truffle_backend_*` modules implement RHI contracts behind backend ownership.
  The null, Metal, Vulkan, and OpenGL backends are buildable now. Direct3D
  retains a reserved module/build-option boundary for a later milestone.
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
scope, with Direct3D still intentionally deferred as an extension backend:

- `truffle_render` is ECS-independent and now supports frame-graph orchestration.
- `RenderBatch` and `InstanceLayout` remain the universal renderer input contract.
- `IFrameUploadRing` remains the N-buffered CPU-to-GPU upload primitive.
- `truffle_scene` provides optional ECS extraction via `SceneAdapter`.
- `IPipelineCache` supports material-to-pipeline mapping and variant hash routing.
- `IPipelineReflection` is integrated in Metal and Vulkan contract paths.
- `IPipelineReflection` is integrated in Metal, Vulkan, and OpenGL contract paths.
- `RendererFrameStats` provides per-frame diagnostics for compute/render node and
  batch execution, plus presentation tracking.
- `truffle/core/version.hpp` defines public API version, compatibility policy,
  and deprecation-window semantics.
- CI emits backend parity matrix artifacts for tracked backend contract and
  reflection tests.

ADRs 0004-0008 lock the current design direction; ADR 0009 governs phase
completion policy and execution gating; ADR 0010 defines API compatibility
versioning semantics.

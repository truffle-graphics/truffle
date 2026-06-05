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
- Bind-group layouts can mark uniform/storage buffer bindings as dynamic-offset
  bindings; command binding validates supplied offsets against descriptor arrays,
  buffer ranges, and advertised alignment before work can be recorded. Layouts
  can also specify explicit native descriptor slots for flattened backend binding
  models. Bind groups expose persistent/transient-frame allocation policy and
  layout/group cache keys so higher layers can choose descriptor reuse strategy
  without inventing backend-specific side channels.
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

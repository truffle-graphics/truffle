# Architecture

## Product Shape

Truffle is a modular graphics library, not an application framework. A consumer
chooses a low-level graphics API, rendering helpers, asset metadata, scene
extraction, or diagnostics without inheriting unrelated layers.

1. `truffle_core` provides shared status, version, configuration, and identity
   primitives.
2. `truffle_rhi` defines the canonical backend-neutral graphics contract.
3. `truffle_backend_*` modules implement that contract behind backend ownership.
4. `truffle_render` provides render batches, frame-graph orchestration,
   renderer execution, and pipeline-cache integration without depending on ECS.
5. `truffle_assets` declares backend-free asset, material, texture, geometry,
   group, and tag metadata.
6. `truffle_asset_render` validates and maps asset declarations to metadata-only
   render plans without allocating GPU resources.
7. `truffle_ecs` supplies a general ECS; optional `truffle_scene` converts ECS
   state into render batches.
8. `truffle_diagnostics` provides opt-in, pull-based inspection. Runtime layers
   do not depend on it.

Public contracts live under `include/truffle`. Module implementations live under
`src`, backend code under `src/backends`, build policy under `cmake`, and
consumer proofs under `examples` and `tests`.

## RHI 1 Replacement

RHI 1 replaced the preliminary public virtual interfaces without a
compatibility facade. The public API is split across focused headers, with
`include/truffle/rhi/rhi.hpp` retained only as an umbrella include.

Primary RHI 1 families are `Instance`, `Adapter`, `Device`, `Queue`,
`CommandPool`, `CommandList`, scoped render/compute/copy encoders, resources and
views, binding and pipeline objects, synchronization/query objects, `Surface`,
and `Swapchain`.

Public RAII objects are move-only and contain an opaque generation-checked
handle plus backend dispatch ownership. They do not expose native graphics
types. This seam leaves room for a later stable C ABI without adding one now.

The detailed object, threading, lifetime, outcome, synchronization, and
presentation rules are in [the RHI 1 contract](rhi1/README.md). ADR 0011 makes
the cutover decision durable.

## Current Implementation Truth

- Null is a strict contract interpreter and negative-path oracle, not a GPU
  backend.
- Metal's resource and pipeline slices allocate native buffers and selected 2D textures, compile
  native shaders and pipeline state, and prove transfers plus exact graphics
  and compute output with Metal API validation enabled. It also maps timeline
  semaphores to shared events and owns graphics-side `CAMetalLayer` acquisition
  and presentation while the host retains window/lifecycle ownership.
  Unsupported shapes and operations remain explicit capabilities rather than
  logical substitutes.
- Linux Vulkan, Windows D3D12 WARP, and Linux EGL OpenGL/OpenGL ES expose an
  adapter only after real native initialization and command or clear/readback
  smoke succeeds. Vulkan owns buffers, selected device-local 2D textures/views,
  and native transfers. D3D12 owns buffers, device-local textures, native
  descriptor views and transfers, packaged shaders, reflected root signatures,
  graphics/compute pipelines, native draw/dispatch commands, explicit barriers,
  timeline fences, and timestamp/occlusion queries; the EGL profiles own
  buffers/views and native transfers. These remain narrow matrix foundations:
  WSI, presentation, and unadvertised resource operations are unsupported.
- WebGPU and WebGL2 have explicit source targets. WebGPU remains unavailable;
  WebGL2 has an Emscripten context path but no browser evidence, so both remain
  `source_only` in repository maturity.
- `backend_platform_support()` publishes repository evidence separately from
  runtime adapter discovery. `truffle-rhi-doctor` serializes both views and
  enforces current-host native claims in strict CI lanes. Null is marked
  validation-only and never classified as a GPU backend.

Maturity is tracked per backend-platform pair in
[the support matrix](rhi1/support-matrix.md). Capability support and maturity
are separate dimensions.

## Consumption Levels

### Level 1 — Graphics API

Link `truffle_core`, `truffle_rhi`, and one enabled backend. The application
manages GPU resources, synchronization, and presentation directly.

### Level 2 — Rendering

Add `truffle_render`. Build `RenderBatch` objects from any data source and
execute a `FrameGraph`. Use upload/readback resources directly, or add
`truffle_asset_render` for metadata-to-layout planning.

### Level 3 — Scene Convenience

Add `truffle_ecs` and `truffle_scene`. `SceneAdapter` extracts camera, light,
and batch state from an ECS world. This is one first-party ingestion lane, not
a requirement for RHI or renderer consumers.

## Render Data Lanes

```text
ECS extraction
  ecs::World -> SceneAdapter -> RenderBatch[]

Caller-owned bulk data
  typed arrays / streams -> upload allocation -> RenderBatch[]

GPU-resident data
  persistent or generated GPU resources -> RenderBatch[]
```

All lanes converge at renderer-facing batches and frame-graph nodes. The
renderer does not infer which lane produced a batch. `InstanceLayout` describes
runtime channels, bindings, offsets, strides, and separate/interleaved storage.

`truffle_asset_render` preserves declared stream metadata and validates
material-required attributes. It does not guess shader semantics, compile
shaders, allocate resources, or own backend state.

## Frame Graph And Diagnostics

`FrameGraph` models compute and render nodes, explicit dependencies, and
resource-usage hazards. It resolves deterministic topological order and rejects
cycles before recording.

Diagnostics remain pull-based. Callers can request snapshots or reports for
assets, plans, batches, frame graphs, renderer statistics, and debug-overlay
declarations. Diagnostics install no global hooks and do not become a runtime
dependency of render or scene modules.

## Host Boundary

The application owns native windows, views, layers, event loops, input,
application lifetime, and resize policy. It lends an appropriate native surface
handle to RHI and keeps it alive until the RHI `Surface` and its swapchains are
destroyed.

RHI owns graphics-side surface capabilities, swapchain images, acquisition, and
queue presentation. It does not introduce a window module as part of RHI 1.

## Dependency Boundary

Core runtime code depends only on C++20 and the standard library. Optional
backends and shader tools acquire only their own pinned dependencies. Checked-
out bundled source is the default; configure never downloads source. Platform
SDKs are explicit prerequisites, and expert system-package mode is deliberate
rather than an implicit fallback.

The Vulkan target compiles pinned Vulkan-Headers and volk source directly into
its own static library. Neither headers nor loader shim appear in
`Truffle::RHI` or disabled-backend consumer requirements. D3D12 uses only the
Windows SDK; EGL/GL development packages are discovered only by enabled Linux
GL-family targets.

The complete policy and dependency groups are in
[the RHI 1 dependency specification](rhi1/dependencies.md).

## Shader Boundary

RHI consumes a format-neutral `ShaderPackage`. The optional host-side
`truffle-shaderc` tool assembles and appends precompiled or backend-native
variants into deterministic multi-target packages while recording authoring
language and compiler provenance. Runtime compilation is optional, and target
variants must agree on normalized reflection.

See [the shader package contract](rhi1/shader-package.md).

## Deferred Surfaces

The Simple RHI, compatibility facade, D3D11, WebGL1, legacy GL/GLES profiles,
CUDA/OpenCL, and public proprietary-console implementations are outside RHI 1.
Backend factory boundaries remain extensible for future private platform work.

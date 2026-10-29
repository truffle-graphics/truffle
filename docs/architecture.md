# Architecture

## Product Shape

Truffle is a modular graphics framework, composed of independently usable
libraries. A consumer chooses a low-level graphics API, rendering, an
entity-component library, asset metadata, or diagnostics without inheriting
unrelated modules. Truffle is not an application framework.

1. `truffle_base` provides shared status, version, configuration, and identity
   primitives.
2. `truffle_rhi` defines the canonical backend-neutral graphics contract.
3. `truffle_backend_*` modules implement that contract behind backend ownership.
4. `truffle_renderer` provides render batches, frame-graph orchestration,
   renderer execution, and pipeline-cache integration without depending on
   EnCom. The Renderer owns GPU resources: buffers, textures, uploads, and
   their lifetimes.
5. `truffle_assets` declares backend-free asset, material, texture, geometry,
   group, and tag metadata (provisional; see Planned Modules).
6. `truffle_asset_render` validates and maps asset declarations to metadata-only
   render plans without allocating GPU resources (provisional).
7. `truffle_encom` is Truffle EnCom (ENtity COMponent), a general
   entity-component library that depends only on Base.
8. `truffle_encom_renderer` is the optional EnCom–Renderer integration that
   converts EnCom state into render batches.
9. `truffle_diagnostics` provides opt-in, pull-based inspection. Runtime
   modules do not depend on it.

Developer tools (`truffle-shaderc`, `truffle-rhi-doctor`) are tools, not
modules. They depend on RHI and are never runtime dependencies.

## Module Dependencies

Arrows mean "depends on". Dashed edges are optional; parenthesized modules are
planned, not implemented.

```text
(Engine) --> Renderer
(Engine) -.-> EnCom, (GUI), (Assets)
(GUI) -----> Renderer          (no EnCom dependency; own lightweight hierarchy)
EnComRenderer --> EnCom, Renderer
Renderer --> RHI --> Base
EnCom ----------------> Base
backends --> RHI
```

EnCom and Renderer are each usable alone. Using both through
`truffle_encom_renderer` is the intended fastest combination: aligned data
layouts and change-only uploads are optimization possibilities for that
connector, not guarantees.

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
  smoke succeeds. Vulkan owns buffers, host-visible linear images,
  capability-checked image shapes/views, subresource copies, clear, resolve,
  and blit transfers. D3D12
  owns buffers, device-local textures, native
  descriptor views and transfers, packaged shaders, reflected root signatures,
  graphics/compute pipelines, native draw/dispatch commands, explicit barriers,
  timeline fences, timestamp/occlusion queries, and host-owned Win32/DXGI
  presentation; the EGL profiles own buffers/views, samplers, uncompressed
  color/depth/stencil texture shapes, and native transfers, with core texture
  views and multisample resolve limited to desktop GL. Their shared program
  layer owns native GLSL linking, reflection validation, logical binding maps,
  sampled arrays, dynamic offsets, immutable samplers, push blocks and scalar
  specialization equivalents. The shared graphics-state owner handles
  vertex/index input, instancing, depth/stencil, MRT and dynamic state; desktop
  GL additionally executes indirect drawing and compute. GLES 3.0 reports its
  corresponding negative capabilities and allows identity texture views only.
  These remain native-smoke
  matrix foundations: unadvertised resource operations and
  backend/platform presentation paths are unsupported.
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

Link `truffle_base`, `truffle_rhi`, and one enabled backend. The application
manages GPU resources, synchronization, and presentation directly.

### Level 2 — Rendering

Add `truffle_renderer`. Build `RenderBatch` objects from any data source and
execute a `FrameGraph`. Use upload/readback resources directly, or add
`truffle_asset_render` for metadata-to-layout planning.

### Level 3 — EnCom–Renderer Integration

Add `truffle_encom` and `truffle_encom_renderer`. `EnComRenderAdapter` extracts
camera, light, and batch state from an EnCom world. This is one first-party
ingestion lane, not a requirement for RHI or renderer consumers.

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

Diagnostics separate three concerns. Each module owns its own error and
lightweight profiling primitives; instrumentation stays optional and detailed
tracing can be compiled out. Collectors, reports, and tools are separate opt-in
consumers. Expanded GPU/renderer/EnCom profiling is planned but not an
immediate priority.

Diagnostics remain pull-based. Callers can request snapshots or reports for
assets, plans, batches, frame graphs, renderer statistics, and debug-overlay
declarations. Diagnostics install no global hooks and do not become a runtime
dependency of Renderer or EnCom-Renderer modules.

## Host Boundary

The application owns native windows, views, layers, event loops, input,
application lifetime, and resize policy. It lends an appropriate native surface
handle to RHI and keeps it alive until the RHI `Surface` and its swapchains are
destroyed.

RHI owns graphics-side surface capabilities, swapchain images, acquisition, and
queue presentation. Truffle does not implement a windowing framework. Renderer
and future Engine consumers pass window handles or offscreen targets. An
optional windowing convenience that wraps GLFW, SDL, or native APIs is a
deferred idea, not a planned RHI 1 module.

## Dependency Boundary

Backend-neutral runtime code depends only on C++20 and the standard library.
Optional backends and shader tools acquire only their own pinned dependencies. Checked-
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

## Architecture Guardrails

The CMake target graph is an enforceable part of the architecture, not merely a
diagram. `tools/check_architecture.py` rejects production-target dependencies
and public-header imports that violate the documented layer direction. Run it
locally for relevant changes; the optional manual Build workflow includes the
same guard. The tool also has a non-blocking source
length report: crossing its threshold means "review cohesion", not "this is a
God file".

Responsibility cohesion requires engineering judgment and is reviewed through
[`architecture-review.md`](architecture-review.md). Every production-code plan
names the owning module, allowed dependencies, non-goals, and public-contract
effect. Small files are reviewed when they combine independent owners,
lifecycles, or reasons to change; large files may remain cohesive when they
have one clear owner and responsibility. Orchestrators may coordinate interfaces
and lifecycle transitions, but detailed subsystem behavior remains with its
owning implementation.

## Shader Boundary

RHI consumes a format-neutral `ShaderPackage`. The optional host-side
`truffle-shaderc` tool assembles and appends precompiled or backend-native
variants into deterministic multi-target packages while recording authoring
language and compiler provenance. A separately enabled, target-private glslang
dependency adds offline GLSL/GLSL ES to SPIR-V compilation without changing the
runtime or exported package dependencies. Runtime compilation is optional, and
target variants must agree on normalized reflection.

See [the shader package contract](rhi1/shader-package.md).

## Planned Modules

These are agreed directions, not implementations. Details stay undecided until
their own design issues.

- **Engine**: a multipurpose graphics engine above Renderer, optionally using
  EnCom, GUI, and Assets. It supports a host-owned application loop; runner
  conveniences, use-case profiles, and scheduling are undecided.
- **GUI**: a GUI library above Renderer that does not depend on EnCom and keeps
  its own lightweight hierarchy. Its retained/immediate design is undecided.
- **Assets**: file loading and import, separate from GPU resources, which the
  Renderer owns. An asset-to-resource bridge is optional. The module's name,
  architecture, and the future of the provisional `truffle_assets` and
  `truffle_asset_render` targets are decided later.

Priority after RHI 1 is the full RHI, then Renderer, then Engine.

## Deferred Surfaces

The Simple RHI is deferred: a beginner-friendly, OpenGL-like convenience layer
over the full RHI whose parts can be replaced incrementally with full-RHI usage.
Further developer-tool work waits until RHI and Renderer reach a minimum usable
level.

The compatibility facade, D3D11, WebGL1, legacy GL/GLES profiles,
CUDA/OpenCL, and public proprietary-console implementations are outside RHI 1.
Backend factory boundaries remain extensible for future private platform work.

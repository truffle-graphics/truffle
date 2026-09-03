# Truffle — Project Context

## Intent

Truffle is a CMake-first layered graphics library workspace. It provides
embeddable graphics contracts and independently linkable modules so consumers
can choose their own application host, windowing policy, simulation model, and
asset workflow.

## Non-Goals

- Truffle does not own native windowing, input policy, application lifetime, or
  a consumer's simulation model.
- Low-level runtime modules do not depend on optional diagnostics, tooling, or
  ECS layers.
- The library does not force all consumers through one scene, asset, or render
  ingestion path.
- The future Simple RHI is not part of the RHI 1 replacement.

## Current State

The repository is replacing its preliminary RHI with **Truffle RHI 1** under
milestone `Truffle RHI 1` and tracking issue #25. The breaking public object and
dispatch cutover plus the resource, ShaderPackage, binding, and graphics/compute
pipeline, explicit synchronization, and presentation slices are complete.

Current evidence is narrower than the old roadmap claimed:

- Null implements the RHI 1 foundation as a strict validation backend, not a
  GPU backend.
- Metal discovers a native device and passes deterministic resource readback plus
  triangle, textured binding, depth/MRT/MSAA, indexed/instanced/indirect, and
  compute-to-render proofs with Metal API validation enabled. Native shared
  events and `CAMetalLayer` tests also prove ordered submission, timeline
  synchronization, acquisition, presentation, resize, and recoverable status
  paths. Its macOS maturity remains `native_smoke`; host-window integration,
  physical device-removal evidence, broader Apple-platform execution, and full
  native conformance remain.
- The #33 matrix work gives Linux Vulkan a pinned private headers/volk loader
  path, native buffers, selected device-local 2D textures/views, and padded-row
  transfers; Windows D3D12 owns native upload/readback/default-heap buffers,
  device-local textures/views, buffer/texture transfers, ShaderPackage-backed
  root signatures, graphics/compute pipelines, explicit barriers, native
  timeline fences, and timestamp/occlusion queries on WARP; and Linux
  OpenGL/OpenGL ES own native EGL buffers and transfers after surfaceless
  clear/readback initialization. These adapters report only the capabilities
  they implement. Remaining WSI, presentation, and unadvertised resource gaps
  stay unsupported rather than simulated.
- ShaderPackage 1.0 is a deterministic compiler-free runtime contract with a
  canonical manifest/blob container, cross-target reflection validation,
  capability gates, and native-override selection. Optional
  `truffle-shaderc` assembles precompiled or native variants without fetching
  or linking a compiler toolchain.
- WebGPU and WebGL2 now have explicit source-only targets; WebGL2 contains an
  Emscripten browser-context path but has no browser execution evidence yet.
- The public support table and `truffle-rhi-doctor` expose matrix maturity and
  live initialization independently. Null is always validation-only and never
  a GPU backend.
- The package, macOS, Ubuntu, and Windows warning-clean CI baseline was restored
  by PR #36.

## Architecture And Boundaries

`truffle_core` supplies shared primitives. `truffle_rhi` defines the one
canonical backend-neutral graphics contract; `truffle_backend_*` modules own
backend implementation. Rendering, assets, scene extraction, and diagnostics
remain layered and independently linkable. Consumers can work directly with
RHI, with render batches, or with the optional ECS scene adapter.

RHI 1 uses move-only RAII objects over opaque generation-checked handles and a
backend dispatch table. It separates portable typed results from optional
backend diagnostics, owns explicit resource/synchronization/presentation
mechanisms, and leaves renderer descriptor caching and recovery policy above
RHI.

The host owns native windows/surfaces, event loops, input, resize policy, and
application lifecycle. Diagnostics remain opt-in and pull-based. Asset
declarations and asset-to-render planning remain metadata layers until a
consumer selects concrete loading, upload, and backend behavior.

## Constraints And Durable Decisions

- RHI 1 is an immediate breaking replacement with no compatibility shim or
  parallel public API.
- Required and optional features are negotiated explicitly; unsupported work
  never reports simulated success.
- Resource creation and queue submission are thread-safe. Command pools,
  recording command lists, active encoders, and descriptor-arena mutation are
  single-thread-owned.
- Dependencies are backend-optional and pinned. Bundled checked-out source is
  the reproducible default; configure performs no network fetches.
- Backend-platform maturity advances only through objective native evidence.
  Shared contract tests and compilation are necessary but not support claims.
- C++20 and CMake remain canonical. No C ABI is published in RHI 1.

## Validation And References

- `README.md` contains build and consumption guidance.
- `docs/architecture.md` defines module and host boundaries.
- `docs/rhi1/` defines the RHI 1 contract, dependencies, shader packages, and
  maturity matrix.
- `docs/roadmap.md` is the active issue-aligned program sequence.
- `docs/planning.md` defines the durable issue, Project, and decision-tracking
  workflow.
- `docs/adr/0011-rhi-1-replacement-contract.md` records the breaking decision.
- `docs/handoff.md` is the rolling cross-machine work status and must be
  updated before an AI-authored commit.

## Next Meaningful Direction

Continue issue #33 from the native initialization matrix into backend-owned
resources, shaders, synchronization, WSI/presentation, and platform lanes;
promote no pair beyond its recorded evidence. Execute from the focused #33
sub-issues in the Truffle delivery project rather than from an untracked local
plan.

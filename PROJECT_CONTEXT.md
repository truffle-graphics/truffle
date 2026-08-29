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
dispatch cutover is complete; capability slices beyond the foundation remain in
progress.

Current evidence is narrower than the old roadmap claimed:

- Null implements the RHI 1 foundation as a strict validation backend, not a
  GPU backend.
- Metal is the only implementation that currently calls a native graphics API;
  it discovers a native device and passes buffer and texture upload/copy/
  readback proofs with Metal API validation enabled. Its macOS maturity is
  `native_smoke`; full native conformance and presentation remain.
- Vulkan, OpenGL, and Direct3D backend targets explicitly return `unsupported`
  and report no adapters. Their previous CPU simulators have been removed; the
  future native implementations remain `source_only`.
- ShaderPackage 1.0 is a deterministic compiler-free runtime contract with a
  canonical manifest/blob container, cross-target reflection validation,
  capability gates, and native-override selection. Optional
  `truffle-shaderc` assembles precompiled or native variants without fetching
  or linking a compiler toolchain.
- WebGPU, OpenGL ES, and WebGL2 targets are planned but not implemented.
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
- `docs/adr/0011-rhi-1-replacement-contract.md` records the breaking decision.
- `docs/handoff.md` is the rolling cross-machine work status and must be
  updated before an AI-authored commit.

## Next Meaningful Direction

Complete immutable bindings, pipeline layouts, and graphics/compute pipeline
contracts under issue #30, then connect package variants to each native backend
as those pipeline implementations land.

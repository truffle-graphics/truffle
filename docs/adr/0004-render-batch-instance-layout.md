# ADR 0004: Render Batch And Instance Layout

## Status

Accepted

## Context

The Phase 1 renderer coupled its data types directly to ECS: `ExtractedFrame`
held `encom::Entity` references for cameras and lights, `RenderItem` carried an
entity alongside its render data, and `truffle_renderer` included
`truffle/encom/world.hpp` as a hard compile-time dependency. This made it
impossible to use the renderer without ECS, which contradicts ADR 0001
(independently linkable layers) and ADR 0002 (renderer-facing design is
data-oriented).

Additionally, all data moved through a CPU intermediate format before reaching
the GPU. For large workloads such as a million sensor detections, a per-entity
extraction loop dominates frame time and makes direct bulk upload impossible.

## Decision

Truffle defines `RenderBatch` and `InstanceLayout` as the universal renderer
input contract. Every data source — ECS extraction, direct bulk upload, or
GPU-resident buffers — produces `RenderBatch` objects. `truffle_renderer` has
zero compile-time dependency on `truffle_encom`.

`InstanceLayout` declares at runtime which data channels are present, which
buffer binding slot each occupies, and the per-instance byte stride. The binding
model is configurable:

- `Separate` (SoA): each channel in its own buffer binding, cache-friendly for
  compute passes and partial-channel operations.
- `Interleaved` (AoS): all channels packed into one binding, expressed as a
  degenerate case of the same `ChannelDesc` slot model.

This maps directly to GPU vertex-input binding concepts so no abstraction cost
is introduced when translating to pipeline state.

Three data lanes feed `RenderBatch` objects into the renderer:

- **Lane A — ECS extraction**: `truffle_encom_renderer` adapter walks an `encom::World`,
  writes extracted channel data into a `FrameUploadRing`, and returns a vector
  of `RenderBatch` objects pointing into ring memory.
- **Lane B — Bulk direct upload**: callers write their own typed arrays into a
  `FrameUploadRing` allocation with a single mapped write. No per-item
  conversion loop.
- **Lane C — GPU-resident**: callers provide handles to buffers already on the
  device. Zero upload cost.

An `IPipelineCache` interface accepts an `InstanceLayout` plus a `MaterialId`
and returns a pipeline. The null implementation stores a single null pipeline.
Full shader-variant compilation is deferred to a later milestone.

## Consequences

- `truffle_renderer` links zero ECS targets.
- The ECS adapter lives in the optional `truffle_encom_renderer` module.
- Bulk workloads can bypass per-entity CPU extraction entirely.
- Future GPU-driven and indirect-draw paths can build on the same `RenderBatch`
  model by using `DrawKind::Indirect` or `DrawKind::IndirectIndexed`.
- `InstanceLayout::hash()` provides a stable key for pipeline cache lookup and
  future shader permutation selection.

# Architecture Decision Records

Architecture decisions that need long-term tradeoff history belong here.

- `0001-layered-product-boundary.md` defines independently consumable Truffle
  layers and the host boundary.
- `0002-render-data-ingestion-lanes.md` keeps renderer-facing design open to ECS
  extraction and high-throughput data paths.
- `0003-backend-and-dependency-foundation.md` records the preliminary backend
  ownership and dependency direction; ADR 0011 supersedes its sourcing policy
  for RHI 1.
- `0004-render-batch-instance-layout.md` defines `RenderBatch` and
  `InstanceLayout` as the universal renderer input, decouples `truffle_render`
  from `truffle_ecs`, and establishes the three data-lane model.
- `0005-frame-upload-ring.md` establishes `IFrameUploadRing` as the
  backend-neutral N-buffered CPU-to-GPU upload primitive at the RHI layer.
- `0006-gpu-transform-hierarchy-direction.md` reserves `LocalTransform` and
  `ParentIndex` channel kinds and defers GPU compute-pass implementation to
  Phase 3/4.
- `0007-render-pass-model.md` adopts an inline `RenderPassDesc` model
  (Metal-style) over pre-compiled pass objects, mapping cleanly to
  `MTLRenderPassDescriptor` and leaving a Vulkan caching path open.
- `0008-frame-graph-orchestration.md` introduces frame-graph node orchestration
  to execute compute and render passes in a unified frame pipeline.
- `0009-phase-completion-governance.md` defines mandatory implementation,
  testing, CI, and documentation gates before any phase can be marked complete.
- `0010-api-compatibility-versioning.md` establishes Truffle's code-level API
  version contract and compatibility semantics used by CI gates.
- `0011-rhi-1-replacement-contract.md` adopts the breaking RHI 1 object,
  dispatch, result, threading, lifetime, dependency, shader, and evidence-based
  backend maturity direction.

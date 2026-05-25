# ADR 0008 — Frame Graph Orchestration

**Status**: Accepted  
**Date**: 2026-05-25

---

## Context

As Truffle evolves in Phase 5, the rendering requirements are extending beyond a single forward swapchain output. The integration of the GPU transform compute pass (Phase 5A) revealed the need for multi-pass execution (compute passes before render passes). Support for shadow maps, post-processing, and multi-camera views demands an orchestration layer that represents the frame as a sequence of passes.

Since Truffle is an embeddable infrastructure, the orchestration API needs to be lightweight, un-opinionated about the surrounding application logic (e.g. FrameKit), and capable of utilizing the inline descriptor model (`RenderPassDesc`) established in ADR 0007.

## Decision

Introduce a minimal `FrameGraph` data structure describing a sequence of compute and render execution nodes.

1. **`IFrameGraphNode` Interface**: Base interface for any pass recorded in the frame.
2. **`ComputePassNode`**: A concrete node that accepts a `TransformComputePassDesc` and an associated `TransformComputePass` to dispatch before fragment work.
3. **`RenderPassNode`**: A concrete node defining a single native `RenderPassDesc` (or resolving to the swapchain) and a vector of `RenderBatch` elements. It models a single topological render phase (e.g., G-Buffer, Opaque, UI).
4. **`FrameGraph`**: An aggregation of nodes built per frame by the consumer (or SceneAdapter) and submitted to the new `Renderer::execute(const FrameGraph& graph, rhi::ISwapchain* swapchain)` entry point.

Instead of heavy automatic resource aliasing/virtual resources seen in highly complex AAA engines, this first increment relies on the user (or ECS layer) allocating and binding actual `rhi::ITexture` and `rhi::IBuffer` views. 

## Consequences

- Replaces the hardcoded single-pass `Renderer::render()` implementation.
- Provides a scalable model where the application can inject arbitrary compute operations inside the frame.
- Explicit lifecycle control is granted back to the caller instead of bloating `Renderer` signatures.
- Simplifies testing since tests can construct synthetic frame graphs linking null backend operations.

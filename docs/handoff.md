# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit so
status, decisions, verification, next steps, and open risks remain current as
applicable.

Do not store raw AI transcripts, secrets, personal-only notes, or machine-private
details here. Promote durable project direction into the roadmap, architecture
docs, charter, or ADRs instead of leaving it only in this working document.

## Current Focus

Transitioning into **Phase 6**: Extended Engine Integrations.
Frame Graph is stable, executing on default implementations.

## Current Work Status

- **Phase 1 to 4** are marked strictly Complete. Refer to `docs/roadmap.md` for historical feature deliverables.
- **Phase 5A** — GPU Transform Hierarchy (Complete).
  - Created `TransformComputePass` and `TransformComputePassDesc` exposing a high-level shader dispatch orchestration.
  - Implemented compute capabilities on Metal backend.
- **Phase 5B** — Frame Graph Orchestration (Complete).
  - Designed and merged `FrameGraph`, `ComputePassNode`, and `RenderPassNode` abstractions in `truffle/render/frame_graph.hpp`.
  - Re-wired `Renderer::render()` to accept `FrameGraph`, dynamically interleaving backend render passes and compute passes.
  - Authored ADR 0008 validating architecture decisions surrounding the node graph.
  - All test consumers correctly use the Frame Graph injection model instead of passing raw batches.

## Relevant Decisions And Constraints

- Truffle is embeddable graphics infrastructure, not an application host or a dedicated game engine.
- Public rendering flow stays backend-neutral; production GPU backends are later roadmap work.
- `FrameGraph` owns high-level order, not rendering states. Individual graph nodes retain responsibility for executing `begin_render_pass` alongside backend queues.
- `truffle_render` must have zero compile-time dependency on `truffle_ecs`. `truffle_scene` is the designated ECS-to-render bridge.
- `RenderBatch` and `InstanceLayout` are the universal renderer input contract.
- Keep active handoff state curated and public-safe. Lasting decisions belong in stable docs or ADRs.
- Normal feature and fix work targets protected `develop`; stable promotion goes through `master`.

## Last Verified Commands And Checks

Verified on MacOS Apple Silicon (Phase 5B Frame Graph orchestration, `feat/phase5b-frame-graph`):

```sh
cmake --preset dev  -DTRUFFLE_BUILD_BACKEND_METAL=ON
cmake --build --preset dev
ctest --preset dev   # 12/12
cmake --preset ci   -DTRUFFLE_BUILD_BACKEND_METAL=ON
cmake --build --preset ci  # warnings-as-errors clean
```

12 tests: 3 host workspace smoke, ECS, null RHI (+ indexed draw), render flow,
render batch, frame ring, scene adapter, Metal backend (+ indexed draw + compute),
package consumer, transform compute tests (+ Frame Graph integration).

## Next Resume Steps

1. Read `AGENTS.md`, README, contributor guidance, and architecture docs.
2. Confirm `feat/phase5b-frame-graph` is merged and PR accepted.
3. Review Phase 6 priorities.
4. Update this handoff before stopping on another machine.

## Open Questions Or Risks

- Frame Graph dependency modeling between Nodes is non-existent. Rendering logic executes completely linearly in defined code iteration.

## Curated Ideas Parking Lot

- Use this short list for project-relevant ideas that need another pass before they become roadmap items, ADRs, or implementation work.

## Promotion Rule

Move stable architecture, scope, roadmap, governance, or tradeoff decisions out
of this handoff when they become durable project truth. Use the charter,
architecture docs, roadmap, contributor guidance, or an ADR as appropriate.

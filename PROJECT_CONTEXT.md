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
- The library must not force all consumers through one scene, asset, or
  rendering ingestion path.

## Current State

The current roadmap foundation is complete through its contract, validation,
packaging, and backend-confidence phases. Active work continues on the
Direct3D extension track and on maintaining backend parity without weakening
the shared RHI contracts.

## Architecture and Boundaries

`truffle_core` supplies shared primitives. `truffle_rhi` defines backend-neutral
graphics contracts; `truffle_backend_*` modules implement them. Rendering,
assets, scene extraction, and diagnostics remain layered and independently
linkable. Consumers can work directly with RHI, with render batches, or with
the optional ECS scene adapter.

The host owns native surfaces and event loops. Diagnostics are opt-in and
pull-based. Asset declarations and asset-to-render planning remain metadata
layers until a consumer selects concrete loading, upload, and backend behavior.

## Constraints and Durable Decisions

Keep public contracts backend-neutral, validate invalid states explicitly, and
preserve the separation between low-level consumers, renderer consumers, and
full framework consumers. Treat backend parity and contract tests as product
requirements rather than optional polish.

## Validation and References

- `README.md` contains build, test, and consumption guidance.
- `docs/architecture.md` defines module, data-flow, host, diagnostics, and
  dependency boundaries.
- `docs/adr/` records durable architecture decisions.
- `docs/handoff.md` is the rolling cross-machine work status and must be
  updated before an AI-authored commit.

## Next Meaningful Direction

Complete the Direct3D extension track and continue low-level graphics work only
when it preserves clear RHI contracts, backend parity, and independently usable
consumer layers.

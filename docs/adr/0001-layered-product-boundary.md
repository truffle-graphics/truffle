# ADR 0001: Layered Product Boundary

## Status

Accepted

## Context

Truffle must be useful as a low-level graphics library and grow into a capable
rendering framework without forcing every consumer through the same highest
level API. Consumer applications also need to retain ownership of their host
loop, input policy, native windowing, and simulation model.

## Decision

Truffle is one layered product with independently linkable CMake targets. Base,
RHI, backend, renderer, scene-integration, and future framework-facing layers
build upward without hiding the lower layers from consumers.

Application hosting stays outside Truffle. Consumers provide native surface
boundaries and choose which Truffle layers to link.

## Consequences

- Low-level tools can use RHI and backend targets without adopting future
  framework modules.
- Higher layers may add materials, asset-facing boundaries, debug hooks, and
  tool orchestration above renderer and RHI contracts.
- Architecture and CMake work must keep module boundaries explicit as features
  grow.

# ADR 0001: Modular Framework Boundary

## Status

Accepted (amended by #161: modular-framework wording and module names)

## Context

Truffle must be useful as a low-level graphics library and grow into a capable
rendering framework without forcing every consumer through the same highest
level API. Consumer applications also need to retain ownership of their host
loop, input policy, native windowing, and simulation model.

## Decision

Truffle is a modular graphics framework, composed of independently usable
libraries with independently linkable CMake targets. Base, RHI, backend,
Renderer, EnCom, EnCom–Renderer integration, and future higher-level modules
build upward without hiding the lower libraries from consumers.

Application hosting stays outside Truffle. Consumers provide native surface
boundaries and choose which Truffle libraries to link.

## Consequences

- Low-level tools can use RHI and backend targets without adopting higher
  modules.
- EnCom and Renderer are usable independently; their integration is an
  optional connector, not a mandatory scene system.
- Higher modules may add materials, asset-facing boundaries, debug hooks, and
  tool orchestration above Renderer and RHI contracts.
- Architecture and CMake work must keep module boundaries explicit as features
  grow.

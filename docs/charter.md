# Truffle Charter

## Purpose

Truffle is a modular graphics framework, composed of independently usable
libraries. It gives C++ applications GPU abstraction, rendering, an
entity-component library, and future higher-level modules without requiring a
game-engine object model or an application framework.

## In Scope

- Backend-neutral RHI and renderer contracts.
- Render data paths that can support extracted ECS state and future
  high-throughput bulk or GPU-oriented workflows.
- Truffle EnCom (ENtity COMponent), a general entity-component library suitable
  for tools, simulations, and games, usable without the renderer.
- Optional EnCom–Renderer integration that moves EnCom data to the GPU
  efficiently when a consumer uses both.
- Future higher-level modules (Engine, GUI, Assets) that build above lower
  Truffle libraries without making them mandatory.
- Presentation contracts for consumer-owned native surfaces.
- Validation backends, examples, developer tools, and contract tests that keep
  backend work honest.

## Out of Scope

- Native windowing, input policy, and application-host ownership. A future
  optional windowing convenience may wrap GLFW, SDL, or native APIs, but
  Truffle does not implement a windowing framework.
- A game editor, gameplay framework, or game-specific asset model.
- A required dependency on any application or host framework.
- A requirement that consumers adopt Truffle EnCom before using the renderer.
- Backend-specific public API leakage outside explicit capabilities or extensions.

## Integration Boundary

Truffle runs inside a consumer-owned loop and on a consumer-owned native
surface. Applications that combine Truffle with an application or host
framework compose them at their own boundary.

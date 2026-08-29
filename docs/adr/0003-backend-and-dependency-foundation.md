# ADR 0003: Backend And Dependency Foundation

## Status

Superseded by ADR 0011 for RHI 1. Retained as the preliminary backend-layout
decision.

## Context

The null backend validates Truffle contracts now, while production backend work
will span Vulkan, Direct3D, Metal, and OpenGL. Build structure needs to reserve
clear ownership before SDK discovery and backend implementations arrive.

Dependency choices must keep runtime libraries maintainable without making
example-only dependencies part of Truffle itself.

## Decision

Backend ownership lives under dedicated backend modules. The foundation pass
keeps the null backend buildable and reserves disabled CMake options and module
folders for Vulkan, Direct3D, Metal, and OpenGL.

Runtime dependencies prefer Git submodules where practical. A pinned source copy
is acceptable when a submodule does not fit, but it must keep license,
provenance, and build footprint explicit. Example-only dependencies remain
isolated inside their example folders.

## Consequences

- Production backend milestones can add SDK discovery and targets without
  reshaping the top-level build.
- Enabling a reserved production backend option before implementation is an
  explicit configure error.
- Dependency reviews must distinguish runtime dependencies from example-local
  proof dependencies.

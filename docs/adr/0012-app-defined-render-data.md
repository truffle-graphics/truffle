# ADR 0012: Application-Defined Render Data

## Status

Accepted as direction (#204). Refines ADR 0004. Implementation begins with the
minimum Renderer; existing code is unchanged until then.

## Context

ADR 0004 made `RenderBatch` and `InstanceLayout` the universal renderer input
and removed the ECS dependency. Its channel model still names a fixed
vocabulary (`Transform`, `Color`, `Normal`, `TexCoord`, ... plus one
`CustomFloat4`), and `renderer/components.hpp` defines `Transform`, `Camera`,
`MeshRenderer` and light structs.

Truffle targets very different applications: CAD, animation tools, node-based
tools, space, indoor and 2D games, and dense simulation data. They describe
objects differently (one matrix, or separate position/rotation/scale; different
precision; extra domain properties) and light and shade them differently. A
fixed per-object vocabulary forces each application to convert its data into
Truffle's shape on the CPU or GPU every frame. That costs performance and locks
the Renderer to one use case.

The Shader module (#73) already provides reflection of shader inputs and
composition of reusable shader source, so shaders can describe and interpret
data themselves.

## Decision

### Fixed renderer vocabulary

The Renderer owns established rendering concepts: mesh (vertex/index data),
buffer, texture, shader program/pipeline, render pass and targets, view/camera,
and draw. These are stable across use cases and are not made open-ended.

The Renderer may provide convenience types for core concepts, such as a camera
that produces view/projection data, so common setups integrate easily. Using
them is optional; an application may supply its own view data instead.

### Per-object data is application-defined

What each object carries, and what it means, belongs to the application:

1. The application declares a data layout: named fields with formats, offsets
   and stride (for example "64 bytes: float4 position, float4 rotation
   quaternion, float scale, float4 color").
2. The shader declares what it reads; Shader reflection reports it.
3. The Renderer matches the declared layout to reflection by name, validates
   formats, sizes and alignment, and uploads the bytes unchanged.

No mandatory conversion to a Renderer-defined `Transform` or similar type.

### Interpretation lives in shaders

How data is interpreted (for example building a matrix from position, rotation
and scale) is shader code, delivered as reusable composed shader chunks through
the Shader module. Different applications supply different chunks. Common
setups (a world matrix, a directional light) ship as example chunks, not as
built-in Renderer types.

### Lights and materials

Lights and materials are application-defined data plus shader chunks, not
lighting or material models hard-wired into Renderer logic. A CAD viewer, an
animation tool and different games may use entirely different models.

### Scene and ECS types stay outside

The Renderer does not know ECS or scene component types. Integrations such as
`truffle_encom_renderer` translate an application's components (one transform
component, or separate position/rotation/scale, or any other format) into
render data. This keeps ADR 0004's dependency direction.

### Renderer-consumed values

The Renderer may need a small set of facts for its own work: view data for
drawing, object bounds for culling, and a sort key for ordering such as
transparency. These are a small set of optional, named, documented inputs, not
a fixed component model. Features that need them are unavailable when they are
absent, not silently approximated.

### Limits and validation

- GPU format and alignment rules still apply. The Renderer validates layouts
  against them and against reflection and reports explicit errors; it does not
  silently convert or repack data.
- Interpreting data on the GPU is not always cheaper than converting once per
  object on the CPU. Applications remain free to choose either.

## Consequences

- The minimum Renderer matches application layouts against shader reflection
  from the start instead of adding more fixed channel kinds.
- `ChannelKind`'s fixed vocabulary and `renderer/components.hpp` are review
  items for the later Renderer architecture review: likely replaced by named
  application-declared fields, with component types moving to ECS integration.
  ADR 0004's batch, lane and binding-model decisions are otherwise unchanged.
- Shader chunks become the reuse mechanism for common interpretations, so the
  Shader module's composition is part of normal Renderer use, not only hot
  reload.
- The architecture remains open to refactoring after the minimum Renderer; this
  ADR records the data-ownership direction, not a final Renderer design.

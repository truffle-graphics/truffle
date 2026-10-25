# RHI 1 Bindings And Pipelines

Issue #30 establishes the immutable binding, graphics-pipeline, and
compute-pipeline slice. The public contract uses logical shader identities and
capability-reported behavior; a backend must either implement an operation or
return a typed `unsupported` result.

## Binding Model

A shader resource is identified by `(group, binding, arrayElement)`. That
identity is stable across targets. `ShaderBindingMap` translates it to a
backend-native group, binding, and array element without changing pipeline
reflection or application code.

`BindGroupLayout` is immutable and defines resource types, array counts, stage
visibility, minimum buffer sizes, dynamic-offset use, and optional immutable
samplers. `BindGroup` must completely populate every non-immutable layout
element with a compatible resource and usage. Its resources and layout are
retained by recorded commands.

`DescriptorArena` owns explicit bind-group capacity. Arena mutation and reset
are single-thread-owned. Reset starts a new epoch, restores capacity, and
invalidates every bind group allocated from an earlier epoch. Bind groups are
otherwise immutable; update-after-bind is a separate optional capability.

Dynamic offsets are supplied when a group is bound. Their order is the layout's
dynamic entries sorted by `(binding, arrayElement)`. Each offset is added to the
immutable base offset and must preserve the adapter's uniform or storage-buffer
alignment and remain inside the allocation.

Bindless tables and update-after-bind are optional. Their presence is reported
independently from ordinary bind groups. A backend that does not support both
requirements rejects bindless-table creation explicitly.

## Layout And Reflection Agreement

`PipelineLayout` lists group layouts in strictly increasing logical-group order
and declares non-overlapping, four-byte-aligned push-constant ranges per shader
stage. Creation rejects foreign, duplicate, or unsorted group layouts and
ranges beyond the adapter limit.

Graphics and compute pipeline creation checks shader reflection against the
layout. Resource type, array count, minimum buffer size, group identity, and
stage visibility must agree. Push-constant declarations must be covered by the
layout. Every supplied specialization value must have a unique ID and the same
type as a declaration in one of the pipeline's shaders.

Shader packages carry the same normalized reflection, specialization,
push-constant, workgroup, and logical-remap data into `ShaderDesc`. Package and
direct shader creation therefore reach the same validation and native pipeline
path.

## Graphics State And Commands

`PipelineDesc` owns the complete fixed graphics state for the implemented
slice:

- vertex buffer strides, step modes, attributes, formats, and locations;
- primitive topology, patch control-point declaration, fill mode, culling,
  winding, depth clamp, and depth bias;
- independent color-target formats, blend equations, and write masks;
- depth/stencil format, compare/write state, front/back stencil operations, and
  masks;
- sample count, sample mask, and alpha-to-coverage;
- static or dynamic viewport, scissor, blend constant, stencil reference, and
  depth bias.

Render-pass attachments must match the bound pipeline's formats and sample
count. Resolve attachments are single-sample and format-compatible. Draw calls
retain every argument: vertex/index count, instance count, first vertex/index,
base vertex, and first instance. Indexed, instanced, indirect, and indirect-
count forms have distinct validation. A draw is rejected until every dynamic
state selected by its pipeline has been set.

`PipelineCache` is capability-gated. Cache data is opaque and backend-owned; no
portable cache format is implied.

## Compute Contract

Compute shaders declare required and preferred workgroup sizes. A compute
pipeline inherits omitted values from reflection, rejects partial or
contradictory declarations, and validates each dimension and the invocation
product against adapter limits. Direct and indirect dispatch preserve all
three group dimensions. The native backend selects its threadgroup width from
the validated required size rather than from a hard-coded default.

## Current Evidence And Limits

| Backend | Implemented behavior | Explicit limitations |
|---|---|---|
| Null validation | Ordinary immutable groups, descriptor arrays, dynamic offsets, immutable samplers, arena epochs, pipeline layouts, push/specialization constants, complete graphics state, graphics/compute commands, indirect-count validation, and deterministic opaque cache round trips. | Bindless tables and update-after-bind return `unsupported`; Null remains a validation interpreter, not a GPU. |
| Metal on macOS | Native samplers, MSL-source and metallib shaders, function constants, logical binding remaps, immutable groups and arrays, dynamic offsets, push constants, native graphics/compute pipeline state, direct/indexed/instanced/indirect draws, dispatch and dispatch-indirect, MRT, depth/stencil state, and MSAA resolves. | Bindless/update-after-bind, indirect-count, serialized pipeline caches, and tessellation are not advertised. |
| Vulkan on Linux | Native samplers, immutable ordinary bind groups, descriptor arrays, dynamic offsets, immutable samplers, push constants, descriptor-set/pipeline layouts, specialization values, direct/indirect compute, direct/indexed/instanced/indirect draws, depth/stencil, MRT, MSAA resolve, and compute-to-render ordering. | Non-identity SPIR-V binding remaps, bindless/update-after-bind, indirect-count, serialized pipeline caches, depth clamp, non-fill rasterization, and tessellation are not advertised. |
| D3D12 on Windows | Native samplers, root signatures, immutable groups and arrays, dynamic offsets, push constants, graphics/compute pipeline state, direct/indexed/instanced/indirect draws, dispatch, MRT, depth/stencil state, and MSAA resolves. | Bindless/update-after-bind, indirect-count, serialized pipeline caches, tessellation, and non-HLSL/DXIL shader routes are not advertised. |
| OpenGL and OpenGL ES | Native-source shaders, vertex/index input, instancing, depth/stencil, MRT and dynamic state. Build `40229260348` proves exact output independently on both profiles, plus desktop GL indirect drawing and MSAA resolves. | GLES 3.0 does not expose compute, indirect drawing, native subresource views or multisample textures. Binding/compute expansion is awaiting its own native receipt. Caches, tessellation and indirect-count remain unsupported. |

## Native GLSL Binding Convention

GL program reflection is checked against the package's named logical resources.
Uniform and storage buffer names identify GLSL interface blocks; sampled
textures and separate samplers identify the same GLSL combined-sampler uniform.
Descriptor arrays use the reflected base name and array elements. Native slots
are allocated from the pipeline layout, or selected by an explicit flat
`ShaderBindingMap` (`nativeGroup = 0`). Separate texture and sampler mappings
must select the same texture unit. Logical groups and bindings need not equal
native slots. Missing resources, size/type mismatches, conflicting mappings and
unreflected active resources fail pipeline creation.

For example, a logical texture array at `(2, 5)` can name `images`, while logical
samplers `(2, 8)` and `(2, 9)` name `images[0]` and `images[1]`. Remap the texture
elements and corresponding samplers to matching native units:

```glsl
uniform sampler2D images[2];
layout(std140) uniform Params { vec4 color; };
layout(std140) uniform TruffleFragmentPushConstants { vec4 tint; };
```

Push constants use a stage-specific std140 block named
`TruffleVertexPushConstants`, `TruffleFragmentPushConstants`, or
`TruffleComputePushConstants`. Reflected ranges must cover its native block
size, up to 128 bytes. GLSL specialization equivalents are active named scalar
uniforms whose types match specialization reflection. Their defaults and
pipeline overrides are set when the immutable linked program is created; they
are not compile-time GLSL constants or workgroup-size substitutions.

Desktop GL storage blocks and direct/indirect compute use queried native limits
and validate the linked local size against shader reflection. GLES 3.0 keeps
these paths unsupported. GLES permits full identity texture views for sampling,
not subresource or format-reinterpreting views. Storage images remain
unsupported on both profiles. Both profiles own all touched draw/binding state;
GLES requires common per-target blend/write-mask state and rejects base-vertex,
base-instance, non-fill rasterization and depth clamp explicitly.

The validation-enabled macOS suite compares exact pixels or buffer values for a
native triangle, descriptor-array texturing, depth-tested MRT with 4x MSAA,
indexed/instanced/indirect drawing, and compute-to-render storage flow. The
validation-enabled Linux suite separately proves exact Vulkan storage-buffer
compute, textured and indirect drawing, depth-tested MRT/MSAA rendering, and a
compute-written buffer consumed by a later fragment stage. It also exercises
arena reset/retirement and deterministic mismatch/unsupported paths. These
proofs expand native backend slices but do not establish full backend
conformance.

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
| Vulkan on Linux | Native samplers, immutable ordinary bind groups, descriptor arrays, dynamic offsets, immutable samplers, push constants, descriptor-set/pipeline layouts, specialization values, compute pipelines/direct dispatch, and single-sample single-color graphics pipelines with direct/indexed draws. | Depth/stencil, MRT, MSAA, indirect commands, non-identity SPIR-V binding remaps, bindless/update-after-bind, and serialized pipeline caches are not advertised. |
| D3D12 on Windows | Native samplers, root signatures, immutable groups and arrays, dynamic offsets, push constants, graphics/compute pipeline state, direct/indexed/instanced/indirect draws, dispatch, MRT, depth/stencil state, and MSAA resolves. | Bindless/update-after-bind, indirect-count, serialized pipeline caches, tessellation, and non-HLSL/DXIL shader routes are not advertised. |
| OpenGL and OpenGL ES | No binding or pipeline capability is exposed. | Their native adapters currently stop at resource/transfer slices; no Null pipeline is reported as native. |

The validation-enabled macOS suite compares exact pixels or buffer values for a
native triangle, descriptor-array texturing, depth-tested MRT with 4x MSAA,
indexed/instanced/indirect drawing, and compute-to-render storage flow. The
validation-enabled Linux suite separately proves Vulkan compute writes exact
storage-buffer words through a reflected ordinary bind group and a generated
vertex/fragment pair writes an exact render-target pixel. These proofs expand
native backend slices but do not establish full backend conformance.

# Prepared programs in Renderer (#179)

Renderer depends only on RHI/Base. Prepared **Truffle Shader Packages (TSP)**,
including `.tsp` files loaded with `rhi::ShaderPackage::load`, are permanent
first-class inputs. There is no source compiler, EnCom, Assets, GUI, Engine or
window dependency.

## Registration and replacement

```cpp
using namespace truffle;
renderer::PipelineCache programs(device);
renderer::Renderer renderer(device, &programs);

renderer::ProgramDesc request;
request.vertex = {&vertexPackage, target, "vertex_main",
                  rhi::ShaderStage::vertex, configuration};
request.fragment = {&fragmentPackage, target, "fragment_main",
                    rhi::ShaderStage::fragment, configuration};
request.pipeline.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
auto status = programs.register_program(material, request);
if (!status.ok()) {
    // Report status.code/message/detail. No material was changed.
    return status;
}
// Both packages, selection strings and request can now be destroyed.

// For replacement, prepare a complete candidate before making it visible:
auto candidate = programs.stage_program(rebuiltRequest);
if (!candidate.ok()) {
    return candidate.status(); // existing material is unchanged
}
return programs.commit_program(material, std::move(candidate).value());
```

Selections explicitly name target, entry, stage and permutation for each stage.
Vertex and fragment may come from the same package or different packages.
RHI selects prepared variants using its canonical precedence and creates the
native shaders. Renderer owns those shaders and the complete native pipeline.
It retains no package, source, request or caller-local shader pointers.

`stage_shaders` / `register_shaders` also accept a **move-owned** `ShaderBinding`
and an RHI pipeline descriptor for low-level consumers. There are no borrowed
shader registration or compatibility paths. Pipeline-descriptor shader pointers
must be null; layout and optional native cache pointers are borrowed during
staging only. The RHI pipeline retains their payloads. Shader/pipeline status
and native details propagate unchanged instead of becoming null pointers.

Publication accepts only a complete native-validated candidate created by that
cache. A rejected or discarded candidate cannot mutate a working material.
Replacement must preserve the published reflection binding contract, exact RHI
pipeline-layout identity, and attachment formats/sample count. This deliberately
keeps existing descriptor groups and passes usable. An interface-changing
program belongs in another material/configuration slot, with host-prepared
bindings, rather than silently breaking batches. Hosts choose which successful
asynchronous rebuild result is current; Renderer has no file-watching or rebuild
scheduler.

## Cache and graphics state

Each `(MaterialId, variantHash)` is an exact registered configuration slot, not
a lossy shader identity hash. Use the same `variantHash` in `RenderBatch`; missing
slots fail explicitly. All exposed static RHI graphics state (vertex input,
topology, blend/write masks, rasterization, depth/stencil, MSAA, specialization
values, static viewport/scissor) is fixed inside the staged pipeline.
Replacement always creates a fresh native pipeline, even for the same slot.
Repeated lookups reuse the published pipeline without recreating it.

`get_or_create` returns `base::Result<rhi::Pipeline*>`; package-backed creation
is eager at staging, so lookup cannot accidentally publish a half-built program.
`InstanceLayout` is data-lane metadata, not an implicit mapping from channel names
to native vertex attributes. Describe native attributes explicitly using
`ProgramDesc::pipeline.vertexBuffers`. Layout metadata never substitutes for
shader/state identity. The current pass's complete ordered color formats,
depth/stencil format and sample count are checked against the selected program.
Multiple render targets, depth and MSAA are subject to RHI/backend capabilities.
Invalidating a material removes **all** its configurations; invalidating all
removes all registrations. Lookup then fails until registration.

## Batches and bindings

Vertex/index/indirect buffers remain borrowed batch inputs. Native vertex input
uses `RenderBatch::bindings`; the direct uniform lane is available for low-level
uses, but is not a substitute for descriptor groups required by reflection.
For reflected resources, the host creates RHI layouts, descriptor arenas and
groups, passes the pipeline layout when staging, then supplies:

```cpp
batch.material = material;
batch.instanceCount = 1;
batch.bindGroups = {{.group = 0, .bindings = &group,
                     .dynamicOffsets = {}}};
renderer::FrameGraph graph;
graph.add_node(std::make_unique<renderer::RenderPassNode>(
    pass, std::vector<renderer::RenderBatch>{batch}));
auto status = renderer.render(graph);
```

Renderer rejects missing/duplicate/invalid groups for each batch, and RHI checks
exact layout identity, descriptor resources/ranges and dynamic offsets. Thus a
previous batch's bindings cannot conceal an omitted required group. RHI handles
target remapping; Renderer contains no backend-specific selection assumptions.
Staging rejects unmatched reflection/layouts before publication.

## Lifetimes, threading and limits

The device outlives the cache and Renderer. All cache operations (including
staging), Renderer recording and publication are single-thread-owned; serialize
them on the host rendering thread. Compilation may happen elsewhere and return
prepared packages. Do not retain borrowed pipeline pointers across replacement,
invalidation or cache destruction. RHI recording/submission retains native
payloads, so replacing wrappers does not retire resources still referenced by
recorded or in-flight commands. Batch resource wrappers and descriptor arenas
must remain valid through `render`; arena reset and host resource reuse still
follow RHI synchronization rules.

This is the first independently usable prepared-program Renderer slice, not a
complete rendering engine. Dynamic graphics-state batches and push-constant TSP
programs are explicitly unsupported. Low-level owned shader callers must not
require push constants. Source includes, compilation, dependencies, caching
compiled results and rebuild policy belong to the separate optional Shader
module. #181 will compose that module's results with this staging/publication
boundary. The default shaderless Renderer cache remains a Null-validation aid;
native consumers pass a real `PipelineCache`.

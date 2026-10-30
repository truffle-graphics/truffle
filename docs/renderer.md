# Programs in Renderer (#179, #181)

Renderer depends on Shader, RHI and Base (Renderer → Shader → RHI → Base).
Prepared **Truffle Shader Packages (TSP)**, including `.tsp` files loaded with
`rhi::ShaderPackage::load`, are permanent first-class inputs and need no
compiler. Source programs are compiled in memory through `truffle::shader`
(see [Source programs](#source-programs-181)). There is no EnCom, Assets, GUI,
Engine or window dependency.

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
require push constants. Source includes, compilation, dependencies and caching
compiled results belong to the Shader module, which Renderer uses directly;
there is no intermediate adapter module. The default shaderless Renderer cache
remains a Null-validation aid; native consumers pass a real `PipelineCache`.

## Source programs (#181)

`renderer::SourceProgram` (`truffle/renderer/source_program.hpp`) owns one
`(material, variantHash)` slot whose program is compiled from source:

```cpp
shader::Compiler compiler;
renderer::SourceProgram program(programs, material, state);

renderer::SourceProgramRequest request;
request.vertex.source = {"fullscreen.vert", vertexText};
request.fragment.source = {"palette.frag", fragmentText};
request.fragment.includeRoots = {"shaders/include"};
request.fragment.defines = {{"PALETTE_ENTRY", "0"}};

// Host-triggered, for example once per frame or after an editor save.
auto update = program.rebuild(compiler, request);
if (!update) {
    log(update.error().message); // last working program is still published
}
```

Each `rebuild` compiles exactly the requested stage configurations (only their
defines, never every permutation) through `shader::Compiler` and consumes the
`Compilation` packages in memory; there is no subprocess, CLI or disk round
trip. Includes are re-resolved on every attempt, so an unchanged rebuild is a
Shader cache hit and reports `published == false` without creating a pipeline,
while an edited include is a cache miss that is republished. Before staging,
Renderer checks that each fragment input has a vertex output at the same
location and type and that each vertex input has a pipeline vertex attribute.
The result is then staged and committed through the same `PipelineCache` path
as prepared TSPs, so the binding/layout/attachment contract and native pipeline
creation are validated. Any failure—compile error, include error, interface
mismatch, layout mismatch or native pipeline failure—returns the error and
keeps the last working program; `published_vertex()`/`published_fragment()`
expose the compilations currently in use. Without a compiler adapter
(`TRUFFLE_SHADER_ENABLE_GLSLANG=OFF`) `rebuild` returns `unsupported` and
prepared TSPs keep working.

The host owns the rebuild policy and timing; Shader owns sources, includes,
compilation and caching; Renderer owns GPU objects, validation and publication.
There is no OS file watcher, thread or background service. Graphics state is
fixed per `SourceProgram`; its layout/cache pointers are borrowed for the
object's lifetime. The first compiler route is GLSL/GLSL ES to SPIR-V, so
runtime source compilation currently targets Vulkan; Metal consumes prepared
MSL/metallib TSPs.

## Evidence and consumer

`tests/renderer_program_tests.cpp` checks package selection, distinct stage
packages, move-owned shader registration, exact slots, candidate publication,
binding incompatibility, explicit errors and removal on the Null oracle.
Existing Renderer/frame-graph/EnCom/transform tests remain intact.

`tests/renderer_metal_tests.cpp` requires a native device and uses hand-authored
small MSL fixtures packaged as TSPs, including serialize/load round trips. It
proves complete 16×16 RGBA8 targets: red initial/reused/staged output, green
committed output, black after a write-mask change, and blue from a reflected
uniform descriptor. Missing selection, intentional native shader compilation
failure, intentional native pipeline creation failure, incompatible bindings
and attachments all preserve the working output. Native error codes/details
are retained. A separately recorded old pipeline still submits and produces
red after its published owner is replaced with green.

The helper `tests/renderer_native_test_utils.hpp` uses only RHI/Renderer and
explicit attachment/transfer barriers and is shared by the Metal and Vulkan
Renderer tests.

`tests/renderer_source_program_tests.cpp` runs on the Null oracle in every
build. With compilers disabled it proves `unsupported` and an untouched
prepared slot. With glslang it covers first publication, cache-hit no-ops,
republish after invalidation, include changes, interface/attribute/stage/
layout/include failures that keep the published program, and recovery.

`tests/renderer_vulkan_source_tests.cpp` (built with Vulkan and
`TRUFFLE_SHADER_ENABLE_GLSLANG=ON`, validation layers required) writes a
multi-file GLSL program with nested includes, compiles one configured define at
runtime to SPIR-V and reads back exact 16×16 RGBA8 output: red initial, an
unchanged cache hit reusing the same pipeline, a red list recorded before an
include edit still rendering red after the blue rebuild is adopted, then a
compile error, an interface mismatch and a layout mismatch all keeping blue,
and recovery to green. Runtime GLSL evidence is Vulkan/MoltenVK only; a
genuine native pipeline-creation failure is covered by the Metal prepared test,
not by this Vulkan source test.

```bash
cmake -S . -B build/renderer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DTRUFFLE_WARNINGS_AS_ERRORS=ON -DTRUFFLE_BUILD_TESTS=ON \
  -DTRUFFLE_BUILD_BACKEND_METAL=ON -DTRUFFLE_BUILD_SHADERC=OFF \
  -DTRUFFLE_SHADER_ENABLE_GLSLANG=OFF
cmake --build build/renderer --parallel 4 --target \
  truffle_renderer_program_tests truffle_renderer_source_program_tests \
  truffle_renderer_metal_tests
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 METAL_DEVICE_WRAPPER_TYPE=1 \
  ctest --test-dir build/renderer --output-on-failure \
  -R '^truffle_renderer_(program|source_program|metal)_tests$'

cmake -S . -B build/renderer-shader -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DTRUFFLE_WARNINGS_AS_ERRORS=ON -DTRUFFLE_BUILD_TESTS=ON \
  -DTRUFFLE_BUILD_BACKEND_VULKAN=ON -DTRUFFLE_BUILD_BACKEND_METAL=OFF \
  -DTRUFFLE_SHADER_ENABLE_GLSLANG=ON
cmake --build build/renderer-shader --parallel 4
ctest --test-dir build/renderer-shader --output-on-failure \
  -R '^truffle_renderer_(source_program|vulkan_source)_tests$'
```

See [`examples/prepared_renderer`](../examples/prepared_renderer/README.md) for a
small native file-loaded TSP consumer. No backend maturity claim is changed.

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

Direction (ADR 0012, #204): per-object data, its interpretation, lights and
materials are application-defined and matched to shader reflection; the fixed
`ChannelKind` vocabulary and `components.hpp` are under review. See
[ADR 0012](adr/0012-app-defined-render-data.md).

### Application-defined vertex data and meshes (#207)

Use `ProgramDesc::pipeline.vertexBuffers` to describe your byte layout with
explicit attribute locations, scalar/vector formats, offsets, stride and
vertex/instance step mode. No `ChannelKind` is required. Prepared package
registration checks reflected vertex inputs against these locations and exact
types. Names are diagnostic, not portable binding identities; resource bindings
use group/binding numbers. Renderer does not reflect or guess fields inside
uniform/storage buffers: their packing remains an explicit app-shader contract.

`Mesh::upload()` (`truffle/renderer/mesh.hpp`) accepts vertex byte streams and
optional uint16/uint32 index bytes. It checks stream sizes and CPU index bounds,
then writes those exact bytes into owned upload-domain GPU buffers. There is no
asset parser, per-field conversion, or device-local staging optimization.
`mesh.batch(program, variant)` borrows the final Mesh object's buffer wrappers;
keep it alive and unmoved through `render()`. Layouts stay in the registered
program; the mesh does not assign property semantics.

Batch views have explicit nonzero byte lengths. The registered pipeline's
vertex/instance layouts determine required stream ranges before recording a
draw. Missing index/indirect resources fail instead of falling back to a
different draw kind. `vertexCount` remains the draw count (index count for an
indexed batch); `vertexElementCount` optionally declares the vertex range bound
and is filled by Mesh. For caller-owned GPU indices and indirect arguments, the
producer remains responsible for their contents and GPU-generated range bounds.
`validate_vertex_layout()` and `validate_batch_data()` are also available for
producer-side validation.

### Optional view and depth helpers (#208)

`truffle/renderer/view.hpp` provides `perspective`, `orthographic`, `look_at`,
`multiply` and a column-major `Matrix4`. They are optional math helpers, not an
ECS camera component: supply your own matrix or view buffer instead. Conventions
are right-handed, column vectors, view forward -Z, and projection depth [0,1].
OpenGL-native [-1,1] projection requires an explicit shader/application mapping.
Degenerate and nonfinite camera parameters return errors.

`DepthTarget` (`truffle/renderer/depth_target.hpp`) owns a depth texture.
`resize()` reuses an identical allocation or creates and transitions a new
attachment on the device's graphics queue. Failed resize preserves the previous
texture. Its device must outlive it; borrowed texture wrapper references are
valid only until resize. GPU payload retention remains RHI-owned.

The shared `examples/minimum_renderer/scene.*` application fixture demonstrates
indexed mesh data, arbitrary per-instance center/scale/quaternion/albedo fields,
an application-defined view/light uniform, and composed MSL transform/lighting
units compiled through `SourceProgram`. None of those object or light types is
part of the Renderer API. Native offscreen tests verify depth occlusion in both
instance orders, per-instance properties, lighting changes, camera translation,
perspective projection and target recreation using exact pixels.
The example serializes updates with an explicit graphics completion fence;
general multi-frame residency/upload optimization is not implemented here.

### Host-owned Metal window (#209)

The [minimum Renderer application](../examples/minimum_renderer/README.md)
draws rotating lit cubes without ECS or a GUI module. Cocoa windowing, backing
pixel dimensions, resize policy and the event loop belong to that executable,
not the Renderer library.

`RenderPassNode(desc, batches, true)` uses an acquired swapchain image while
preserving the description's optional depth attachment and color clear/load/store
settings. Its extent comes from the swapchain; the explicit description may
contain at most one color attachment with null texture/resolve pointers.
The original boolean constructor still supports the existing validation-only
headless path. An explicit swapchain description without a swapchain fails.

The bounded native smoke presents 12 frames across three sizes with matching
depth allocations. A shared offscreen proof checks exact pixels, including a
spatial reference for the same cube geometry.

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

### GUI-readiness primitives (#213)

These are the minimum mechanisms an immediate-mode or retained 2D layer (for
example a later GUI module) needs. They add no widget, font, input or layout
policy; vertex formats, atlas contents and shaders remain application-defined.

- **Clip rectangles.** `RenderBatch::scissor` is an optional rectangle in
  pass-attachment pixels (top-left origin), intersected with the pass extent.
  An empty intersection skips the draw and counts
  `RendererFrameStats::renderBatchesClipped`. It requires a program whose
  `PipelineDesc::dynamicState` is exactly `DynamicState::scissor` (the only
  dynamic state the cache accepts); unclipped batches on such a pipeline
  receive the full pass extent. A clip on a static pipeline is rejected.
- **Blending.** Alpha blending is ordinary `PipelineDesc::colorTargets[].blend`
  state on the registered program; no Renderer feature is involved.
- **Sampled textures.** `renderer::SampledTexture` (`texture.hpp`) uploads an
  uncompressed 2D color texture (r8, rg8, rgba8/srgb, bgra8/srgb, rgba16f,
  rgba32f) through a staging copy and waits for it; `update` replaces a
  sub-rectangle (for example a growing glyph atlas). The texture is left
  shader-readable; bind `view()` with an application sampler in an RHI bind
  group.
- **Streamed geometry.** `renderer::StreamBuffer` (`frame_stream.hpp`) copies
  per-frame bytes (vertices, indices, uniforms) into per-slot upload buffers
  and returns `BufferView`s for `RenderBatch` bindings or index buffers; slots
  grow on demand. Sub-ranges of one buffer are expressed through view offsets.
- **Frames in flight.** `renderer::FramePacer` paces slots over one fence.
  `Renderer::render(graph, swapchain, pacer.signal())` signals the frame's
  fence value at submission, and `RendererFrameStats::submitted` tells
  `end_frame` whether to wait for it later:

```cpp
const auto slot = pacer.begin_frame().value();     // waits for this slot's last use
stream.begin_frame(slot);
batch.bindings[0] = stream.write(std::as_bytes(std::span{vertices})).value();
batch.indexBuffer = stream.write(std::as_bytes(std::span{indices}), 4).value();
batch.scissor = rhi::ScissorRect{x, y, width, height};
auto status = renderer.render(graph, swapchain, pacer.signal());
pacer.end_frame(renderer.last_frame_stats().submitted);
```

Null contract tests cover clipping, signals, pacing, stream alignment/growth
and texture validation. A native Metal proof under API/shader validation
checks every pixel of three paced frames: alpha blending over a clear color,
atlas sampling, an atlas update after upload, clip rectangles (including a
fully clipped batch), and rebuilt streamed geometry reusing a slot.

### Pass transitions and render-to-texture (#215)

Frame-graph resource usages only order passes; the Renderer does not track
texture layouts. A pass that writes or samples a texture another pass produced
states its transitions with `RenderPassNode::transition_before(rhi::TextureBarrier)`.
The Renderer records them as one barrier batch immediately before that pass.
A transition without a texture is rejected with `invalid_argument`. Layout
choice (for example `color_attachment` before writing a layer, then
`shader_read_only` before compositing it) stays with the caller, which also
remembers a cached layer's current layout across frames.

Shared exact-pixel proofs render a 16x16 layer, composite it by sampling,
re-render it under a clip rectangle, then reuse it unchanged in a later frame:
on Metal with native MSL under API/shader validation, and on Vulkan with
GLSL-to-SPIR-V under the validation layer. Removing the compositor transition
makes the Vulkan proof fail validation. A Null contract test covers recording
and rejection.

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
complete rendering engine. Dynamic graphics state other than the scissor and
push-constant TSP programs are explicitly unsupported. Low-level owned shader callers must not
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

# Truffle Shader

`truffle_shader` / `Truffle::Shader` owns CPU-side source resolution,
compilation, reflection, compilation caching, and rebuild state. Its public API
is `truffle/shader/compiler.hpp`, namespace `truffle::shader`. It depends only
on RHI/Base and an explicitly enabled private compiler adapter. Renderer owns
GPU objects and safe publication (`renderer::SourceProgram` consumes
`Compilation` results in memory); this module has no backend, Renderer, EnCom,
watcher, application, or tool dependency.

The accepted module direction is Renderer → Shader → RHI → Base. There is no
adapter/integration module between Renderer and Shader. The core Shader target,
public types, configuration, dependency records, cache ownership, and rebuild
state build and link with every compiler adapter disabled; source compilation
then reports `unsupported`.

Enable `TRUFFLE_SHADER_ENABLE_GLSLANG=ON` to use the pinned GLSL/GLSL ES to
Vulkan SPIR-V adapter. This does **not** require building shaderc.
With the option off, `Compiler::routes()` is empty, `available()` is false and `compile()` returns
`unsupported`; prepared Truffle Shader Packages (TSP, `.tsp`) still work through
RHI and Renderer without linking a compiler. No language/compiler is fetched.
An enabled install bundles glslang's static libraries and CMake package so
installed `Truffle::Shader` consumers resolve its private static link closure.
`Truffle::RHI` never links those libraries. Renderer can depend directly on the
compiler-disabled Shader core without acquiring any compiler link dependency.

## Routes and target configuration (#73, #185)

`Compiler::routes()` describes only compiled-in source routes, not package
assembly, all features of an upstream compiler, or live native device support.
Each route lists its accepted language, emitted target, supported stages, and
exact supported Vulkan/SPIR-V configurations; every listed stage/configuration
combination is supported, subject to the reflection limits below.
The borrowed read-only spans have process lifetime. `available()` is the
convenience test for any source route, useful to compiler-optional clients.

| Shader source acceptance | Emitted output | Enabled dependency | Status / verification |
| --- | --- | --- | --- |
| GLSL (core profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; all reported configurations compile and emitted SPIR-V versions are checked |
| GLSL ES (ES profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; same configuration tests |
| Slang, HLSL, WGSL, MSL | None through Shader today | none enabled | Planned #188-#191 under #73; no adapter or capability advertised |
| SPIR-V, DXIL, Metal libraries, backend-native source | None through Shader today | none needed for assembly | Prepared TSP inputs, **not** source-compilation routes |

Implemented typed `CompileRequest::vulkanTarget` configurations:

| `VulkanVersion` environment | `SpirvVersion` emitted version |
| --- | --- |
| `v1_0` | `v1_0` |
| `v1_1` | `v1_0` |
| `v1_1` (default) | `v1_3` (default) |

Vulkan is the target environment/profile, not an OpenGL SPIR-V environment.
GLSL versus GLSL ES selects and validates the input profile; the source's
`#version` controls its language version. This is not a promise to accept
arbitrary valid GLSL features: unsupported reflected constructs fail explicitly.
Unknown version enum values return `invalid_argument`; known but unsupported
combinations (such as Vulkan 1.0/SPIR-V 1.3), languages, targets or stages return
`unsupported`. These checks precede source resolution and compiler entry.
With no compiler enabled, all valid source requests return `unsupported`.
`Compilation::vulkanTarget` reports the effective configuration.

```cpp
const auto routes = truffle::shader::Compiler::routes();
truffle::shader::CompileRequest request;
request.vulkanTarget = {truffle::shader::VulkanVersion::v1_0,
                        truffle::shader::SpirvVersion::v1_0};
```

Default Vulkan 1.1/SPIR-V 1.3 GLSL vertex/fragment output has native
Vulkan/MoltenVK exact-readback evidence through Renderer's existing
`SourceProgram` regression; GLSL ES compute output has existing native Vulkan
package/dispatch evidence. The additional version configurations have CPU
compilation/header/provenance tests, **not** new native execution evidence.
Prepared MSL on Metal, HLSL/DXIL on D3D12, and GLSL/ES on GL-family backends
are independent backend-native package paths, not Shader generation evidence.
No backend maturity is promoted.

The remaining #73 route matrix is **planned, not implemented or advertised**:

| Input / mechanism | Intended output | Tracking / remaining gate |
| --- | --- | --- |
| Portable source via SPIR-V, optional SPIRV-Cross and validation | MSL, desktop GLSL (including macOS 4.1), GLSL ES | #187; target limits/remaps and native Metal/GL readback; GLES compiler validation separate from native evidence |
| Native MSL, optional Apple tooling | MSL / offline Metal library | #188; SDK audit, truthful interfaces/dependencies, native readback |
| HLSL, optional DXC | SPIR-V / DXIL where compiler/host supports it | #189; profiles/reflection, native Vulkan and separately tracked Windows evidence |
| Slang, native modules/imports | Supported SPIR-V first; other outputs gated | #190; compiler audit, imported dependencies, native output; experimental Metal/WebGPU not promoted by emission alone |
| WGSL, optional Tint | WGSL input/output and supported conversions determined by compiler audit | #191; actual directions, language semantics, reflection and compiler validation; no WebGPU execution claim |

These are not an all-input/all-output cross-product promise. Complete reflection, validated
cross-target interfaces/remaps/specialization, reproducible optional compiler
packaging and per-route native conformance remain completion gates under #73
(reflection #186, shared behavior #192, final evidence audit #193).
The completed #178-#181 foundations are not full Shader-module completion.

## On-demand compilation and reload

```cpp
#include <truffle/shader/compiler.hpp>

truffle::shader::Compiler compiler;       // Keep for compilation cache reuse.
truffle::shader::RebuildState rebuild;    // One state per logical shader.
truffle::shader::CompileRequest request;
request.name = "material";
request.stage = truffle::rhi::ShaderStage::fragment;
request.defines = {{"USE_TEXTURE", "1"}};
request.includeRoots = {"shaders/include"};
auto source = truffle::shader::read_source("shaders/material.frag");
if (!source.ok()) { /* report source.status() */ return; }
request.source = std::move(source).value();
auto attempt = rebuild.rebuild(compiler, request);
if (!attempt.ok()) {
    // Report attempt.status(); rebuild.last_good() may still exist.
    // Failure is never returned as a successful cached/last-good compilation.
    return;
}
const auto& compiled = attempt.value();
const auto& variant = compiled.package.desc().variants.front();
// Pass compiled.package, variant.entryPoint/stage/permutation to Renderer/RHI.
// Renderer owns native shader/pipeline creation and in-flight replacement.
```

Re-read the root source and call `rebuild()` when polling. There is no implicit
root file ownership or background watcher. Every attempt runs glslang's own
preprocessor and re-resolves transitive includes, including conditional include
selection. `cacheHit` means parsing/linking/reflection/SPIR-V generation were
avoided, not that I/O/preprocessing were skipped. Results own all data; there is
no disk round trip. `clear_cache()` releases stored compilation results.
Only requested configurations are compiled.

Quoted includes search relative to the including file, then ordered include
roots; system includes search the roots. `read_source()` produces canonical
filesystem identities. Alternatively provide `request.resolver`; it receives
the requested name, including identity, and system/local flag and returns owned
source text with a unique stable identity. That callback wholly owns resolution
policy (including local/system fallback). Keep its identities/content stable
within one attempt. Resolution is snapshotted for the parse after preprocessing.
Nested/reused includes are supported, missing dependencies and active include
cycles fail with diagnostics, and nesting is bounded to 64.
Include roots are search paths, **not an access-control boundary**: the default
filesystem resolver can follow absolute paths, `..`, and symlinks outside those
roots. Hosts compiling untrusted source must supply a resolver that enforces
their own allowed-source policy; this module does not sandbox filesystem access.
Composition uses ordinary GLSL functions, include guards, and preprocessor
defines; this is not a custom module language or preprocessor.

`Compilation::diagnostics` retains the bounded compiler log (up to 16 KiB).
The diagnostic copied into TSP is limited to its first 4096 bytes, respecting
the package's per-message limit without turning successful compilation into a
packaging failure.

Keys are SHA-256 over unambiguous length-framed effective inputs: adapter
identity, pinned compiler version/revision, effective target/environment and fixed resources,
stage, language/profile, entry point, package name, debug option, normalized
defines, ordered roots, preprocessed code, and every resolved identity/content.
Comment-only dependency edits invalidate too. Resolver function addresses are
not identities: their resolved inputs are. Each dependency reports a genuine
SHA-256 and is recorded in TSP provenance. The generated permutation name is a
stable hash of sorted configuration defines (independent of source edits).
The in-memory cache is instance-owned, unbounded until `clear_cache()`, and
never stores failures. It is not a persistent content store.

Compiler provenance preserves glslang's name/version; its revision records the
pinned source SHA plus `truffle-glslang=4;vulkan=VERSION;spirv=VERSION`.
The effective target therefore survives TSP serialization and shaderc inspection
with the current development TSP metadata. Both target options independently
differentiate cache keys, even when generated instructions otherwise match.

## Reflection and limits

The adapter emits SPIR-V first, with optimization disabled, then reflects the
actual module's types, variables, names, member offsets, strides, decorations
and scalar constant words. This includes inactive declarations retained in the
emitted module, not only a live-uniform database. The private SPIR-V reflection
reader is separate from glslang source resolution/compilation and can serve
future SPIR-V-emitting adapters. No shader-text parsing or guessed layout is
used.

| Baseline feature (#186) | Implemented contract / evidence |
| --- | --- |
| Push constants | One range per emitted block/stage, from lowest member offset to the last addressable byte; nonzero offsets, holes, arrays, nested structures and row/column-major matrix strides come from SPIR-V layout |
| Scalar specialization | `bool`, signed/unsigned 32-bit integer, 32-bit float; exact `SpecId`, name, type and default bits (including false/negative values); native defaults and typed overrides |
| Fixed descriptors | Uniform/storage buffer, separate image/texture and sampler arrays; multidimensional descriptor counts multiply in SPIR-V array order; minimum size is **per buffer element** |
| Buffer layout/access | Explicit member offsets and nested array/matrix strides; readonly variable/member decorations; minimum bytes include one element of a runtime-sized **buffer tail**, not an unsized descriptor array |
| Location IO | 32-bit scalars/vectors, matrices, fixed arrays and interface structures/blocks expand recursively to one scalar/vector leaf per occupied location; canonical stage/direction/location, member locations and interpolation survive full package round-trip |
| Compute workgroups | Fixed positive dimensions and optional direct scalar specialization IDs per X/Y/Z axis, including ID zero/shared IDs; defaults/typed overrides resolve before explicit size comparisons, device limits and retained/native dispatch metadata |
| Package/tool metadata | Full directional/internal IO, axis references/defaults and provenance round-trip; shaderc uses/inspects the same library metadata |

Minimum bytes exclude unaddressed trailing alignment padding: a std140 float
array of two elements needs 20 bytes (stride 16, last scalar ends at 20), not
32. The native proof consumes a matrix push range `[16,60)` with column stride
16 and no invented trailing padding. Runtime-sized buffer tails need their
declared prefix plus the first element's addressable bytes. Specialization-
dependent descriptor dimensions or buffer layouts fail rather than pretending
their defaults are fixed.

`ShaderReflection::interfaces` is the canonical owner of **both directions**,
including vertex outputs and fragment inputs, keyed by
`(stage,direction,location)`. `Compilation::interfaces()` borrows that package
metadata; the former `StageInterface` wrapper and duplicate vector are removed.
Selected RHI `ShaderDesc` retains the same records, so prepared packages receive
stage-link and vertex-attribute validation too. A matrix column becomes a vector, arrays repeat their element
footprints, and structure members follow explicit member locations or the
preceding member's footprint. Diagnostic names are emitted instance/member
paths (`values[1].basis[0]`), not linkage identities; buffer names now use
SPIR-V variable/instance names rather than glslang's block-type names.
Separate names or aggregate spellings with identical location/type footprints
therefore agree. Default interpolation is normalized to `smooth`; `flat`,
`noperspective`, `centroid` and `sample` decorations are retained. For the
Vulkan route, fragment inputs own interpolation; vertex-output interpolation
does not impose a separate cross-stage equality rule. Existing Renderer
linkage checks consume the expanded location/type leaves, not aggregate names.
Cross-target **package** checks compare resource/constant/interface semantics,
not diagnostic names, treat conventional 32-bit `floatN`/`intN`/`uintN` and
`vecN`/`ivecN`/`uvecN` leaves as equal (N=2..4), and equate empty/default,
`perspective` and explicit `smooth` interpolation, including reordered known
centroid/sample qualifiers and implicit smooth. Original spellings still
round-trip; unknown type/interpolation strings receive no guessed conversion.
Types, counts, sizes, access, offsets, specialization
defaults/types, locations and non-equivalent interpolation must still agree.

### Workgroup policy and remaining limits

The #186 contract extension changes public RHI/Shader contracts and draft TSP
bytes under the approved [unreleased-format policy](../../docs/rhi1/shader-package-format.md#canonicalization-and-compatibility).
Container/schema identifiers remain `1` / `1.0`; there is no version bump,
legacy reader, migration or dependency-boundary change. The adapter/cache
identity advances to `truffle-glslang=4` because emitted metadata changes.
The implementation awaits independent review; #186 stays open until the parent
accepts it. This is not completion of the Shader module.

`WorkgroupSpecializationIds` contains optional X/Y/Z references into canonical
scalar specialization records. Required size is the default extent. Only
positive signed/unsigned 32-bit integers are valid; every referenced default
must agree. Shared IDs are deduplicated when type/default bits agree, otherwise
rejected. Overrides apply to every axis sharing an ID. Missing/duplicate IDs,
wrong types, zero/negative dimensions and contradictory explicit pipeline sizes
fail. An out-of-limit default can be specialized into a valid extent: limits
apply **after** resolution, with checked total-invocation arithmetic. A matching
default preferred hint follows resolved dimensions; separately authored hints
remain independent. RHI forwards effective axis defaults/overrides to native
backends and retains resolved dimensions for dispatch.

The SPIR-V reader supports literal/direct scalar dimensions in both
`BuiltIn WorkgroupSize` composites and `LocalSizeId`. Expression-derived
dimensions (`OpSpecConstantOp`, including arithmetic on another specialization)
are explicitly unsupported: an axis ID alone cannot describe their dependency
or override semantics. No interpreter, stale-default success or guessed
mapping is introduced. Glslang's GLSL layout syntax also requires literal
`local_size_*` values; real SPIR-V expression metadata has a negative fixture.
Vulkan consumes scalar map entries; native Metal receives actual function
constant values, including defaults, and dispatches the resolved extent.
GL native-source uniform specialization cannot change compile-time local size,
so axis references fail explicitly; D3D12's existing runtime-specialization
rejection also covers effective axis defaults. WebGPU/WebGL2 have no native
compute path here. No new GL/Windows/WebGPU execution evidence is claimed.

Other real limits remain explicit errors, not hidden completion claims:

- Combined image-samplers require one native combined descriptor, but
  `ResourceBindingType`/`BindingType` model separate textures and samplers only;
  splitting one binding would need a real contract/remapping design.
- Subpass inputs need an input-attachment binding type and render-pass/input-
  attachment contract. Texel buffers need a texel-buffer view/binding kind;
  rectangle and other non-RHI texture dimensions are not mapped to 2D textures.
- Unsized descriptor arrays need variable descriptor counts/runtime-array
  allocation semantics; current `arrayCount` is fixed and nonzero.
- Nonzero component/index packing needs component spans/dual-source indices
  and matching validation; each stage/direction currently has only a unique location.
  Explicit component/index zero is harmless and accepted when no overlap exists.
- Specialization/IO leaves and reflected buffer scalars outside the above
  32-bit baseline, specialization-dependent reflected layouts, and unbounded
  IO footprints are unsupported. Expanded IO is bounded to 4096 leaves.
  Resource dimension/format and uniform-versus-readonly-storage distinctions
  are not separate `ResourceBinding` fields; hosts must still supply an
  appropriate typed RHI bind-group layout. This is not full upstream-language
  reflection or native conformance.
- RHI vertex-buffer formats currently cover float32/uint32 scalar/vector
  fetch only. Signed integer varyings still reflect and link, but signed vertex
  attributes need a future `VertexFormat` signed family and native format
  mappings; pipeline validation rejects incompatible fetch instead of
  discarding or pretending to consume the reflected input.
- Extended per-vertex/primitive/view/task IO and explicit-interpolation
  decorations are rejected; a barycentric per-vertex array is not an ordinary
  array occupying consecutive locations. Advanced stage contracts remain
  separately deferred.

Exact CPU fixtures cover offsets, row/column-major/nested layouts, access,
counts, constants, IO expansion, representational errors and package
round-trips. The Apple Silicon Vulkan/MoltenVK proof consumes the unmodified
library package after serialization, derives counts/minimum sizes/push ranges
from it, and checks exact readback for scalar defaults/overrides, matrix push
offsets, uniform/readonly-storage/writable-storage buffer arrays and separate
image/sampler arrays with validation active. A further native package proof
changes local size `2x1x2` to `3x2x2`: two groups write exactly 8/24 values
`21200..21207` / `22300..22323`, leaving the sentinel tail untouched. RHI also
checks the resolved preferred metadata and explicit required dimensions. A
shared-ID native variant changes both X/Y together and writes exactly 16 values
`22200..22215`, with its sentinel tail untouched.
Native Metal prepared metadata proves default `2x2x1` and overridden `3x1x1`
dispatch/function-constant agreement with exact `1220..1223` / `1310..1312`
readback and untouched tails. This does not add a compiler route or promote
backend maturity. Other compiler routes remain #187-#193.

## Threads and lifetime

Each `Compiler`, `RebuildState`, and request mutation is single-thread-owned.
Independent compiler instances may run from different threads; the adapter
serializes glslang entry conservatively. One process-static initialization and
finalization pair spans all instances, avoiding per-request initialize/finalize
races. Join compilation threads before process teardown; clients must not call
glslang process initialize/finalize independently while using this adapter.
Resolvers are synchronous, borrowed only during `compile()`, and must not
re-enter the adapter. Returned values and last-good snapshots own their data
and survive compiler/cache destruction.

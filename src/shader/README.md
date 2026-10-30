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
With all compiler options off, `Compiler::routes()` is empty, `available()` is
false and `compile()` returns `unsupported`; prepared Truffle Shader Packages
(TSP, `.tsp`) still work through RHI and Renderer without linking a compiler.
No language/compiler is fetched.
An enabled install bundles glslang's static libraries and CMake package so
installed `Truffle::Shader` consumers resolve its private static link closure.
`Truffle::RHI` never links those libraries. Renderer can depend directly on the
compiler-disabled Shader core without acquiring any compiler link dependency.

`TRUFFLE_SHADER_ENABLE_METAL_COMPILER=ON` optionally enables in-process native
MSL compilation on supported Apple runtimes; `TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER=ON`
adds offline library output and requires the installed Apple Metal Toolchain.
These are private, separate capabilities; neither is required for prepared
package consumers.
`TRUFFLE_SHADER_ENABLE_DXC=ON` independently enables HLSL 2021 compilation to
SPIR-V/DXIL and requires `TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON`; the additional
HLSL portable routes use the optional Cross/glslang closure.

`TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON` adds real in-process SPIR-V validation.
`TRUFFLE_SHADER_ENABLE_SPIRV_CROSS=ON` requires both glslang and that validator,
and adds portable Metal/GLSL output. All are private, optional dependencies.
Prepare sources explicitly before configuring (configure never downloads):

```sh
git submodule update --init vendor/glslang vendor/spirv-tools \
  vendor/spirv-headers vendor/spirv-cross
```

Compiler-enabled configure requires clean checked-out sources at the documented
pins, so compiler provenance cannot silently describe different source.
SPIRV-Tools' validation library alone is built/exported; optimizer and CLI
libraries are not part of Shader's install closure. SPIRV-Cross installs its
core/GLSL/MSL static closure and CMake configs. Enabled installs include the
new dependencies' license notices; all-off consumers require none of them.

## Routes and target configuration (#73, #185)

`Compiler::routes()` describes only compiled-in source routes, not package
assembly, all features of an upstream compiler, or live native device support.
Each route lists its accepted language, emitted target, supported stages, and
exact supported Vulkan/SPIR-V configurations; every listed stage/configuration
combination is supported, subject to the reflection limits below.
Native routes also list typed `metalTargets`, `glslTargets`, or `glslEsTargets`.
The Vulkan settings on these routes configure the **intermediate**, not the
native output profile.
The borrowed read-only spans have process lifetime. `available()` is the
convenience test for any source route, useful to compiler-optional clients.
`CompilerRoute::experimental` marks generated MSL's explicit safety requirement.

| Shader source acceptance | Emitted output | Enabled dependency | Status / verification |
| --- | --- | --- | --- |
| GLSL (core profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; all reported configurations compile and emitted SPIR-V versions are checked |
| GLSL ES (ES profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; same configuration tests |
| GLSL or GLSL ES, vertex/fragment/compute | macOS MSL 2.1 source | glslang + SPIRV-Tools + SPIRV-Cross | Experimental explicit opt-in; generated graphics and compute execute on Apple M3 Pro |
| GLSL or GLSL ES, vertex/fragment | GLSL 410 core (macOS OpenGL 4.1) | same three optional adapters | Generated resource-backed graphics executes on macOS 4.1; compute rejected |
| GLSL or GLSL ES, vertex/fragment | GLSL ES 300 | same three optional adapters | Emission and real glslang parse/link validation; **no native GLES execution evidence** |
| HLSL 2021, shader model 6.0, vertex/fragment/compute | Vulkan SPIR-V or DXIL | pinned DXC + SPIRV-Tools | In-process compile/validation and emitted-target reflection; Vulkan/MoltenVK graphics and compute execute on Apple M3 Pro |
| HLSL, vertex/fragment/compute | Experimental macOS MSL 2.1 source | DXC + the portable adapters above | Generated graphics and compute execute on Apple M3 Pro |
| HLSL, vertex/fragment | GLSL 410 core / GLSL ES 300 | same portable adapters | GL graphics executes on macOS 4.1; ES has real compiler validation, no native evidence |
| MSL, vertex/fragment/compute | macOS MSL 2.1 source | optional Apple Metal framework adapter | #188; requires macOS 26+ runtime reflection API and Metal device; exact M3 Pro graphics/compute readback |
| MSL, vertex/fragment/compute | macOS metallib | optional Apple Metal framework adapter plus installed Metal Toolchain | #188; actual `metal`/`metallib` compile and exact M3 Pro graphics/compute readback |
| Slang, WGSL | Remaining outputs are route-specific | optional compiler adapters | Tracked #190-#191; not an all-input/all-output cross product |
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
Prepared HLSL/DXIL on D3D12 and prepared GLSL ES on GLES remain independent
backend-native paths, not additional Shader generation evidence.
No backend maturity is promoted.

### HLSL and DXC (#189)

Enable `TRUFFLE_SHADER_ENABLE_DXC=ON` and
`TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON`. glslang is **not** required for
HLSL-to-SPIR-V/DXIL; the additional portable MSL/GL/ES targets require the
existing Cross/glslang options. DXC runs in process for both runtime clients
and shaderc. No compiler executable or subprocess is involved.

```sh
git submodule update --init vendor/dxc vendor/spirv-tools vendor/spirv-headers
git -C vendor/dxc submodule update --init external/SPIRV-Headers \
  external/SPIRV-Tools external/DirectX-Headers
cmake -S . -B build/hlsl -G Ninja -DTRUFFLE_SHADER_ENABLE_DXC=ON \
  -DTRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON -DTRUFFLE_BUILD_SHADERC=ON
```

The source build is deliberately isolated from Truffle's CMake target namespace,
uses already-prepared pinned sources, and builds only `dxcompiler` with bounded
parallelism. Ninja, Python and the platform C/C++ SDK are prerequisites. The
enabled install includes the private compiler shared library and license
closure; no DXC headers become a public requirement. Apple Silicon source-build
and installed-consumer execution are verified. The release has Windows/Linux
binary assets but **no macOS binary**; these artifacts are not silently used as
a fallback. Windows/Linux build execution remains separate evidence.

Set `request.language = rhi::ShaderSourceLanguage::hlsl`, choose an entry point,
and set `request.hlslShaderModel = HlslShaderModel::v6_0` (the default).
The stage selects `vs_6_0`, `ps_6_0`, or `cs_6_0`; source semantics are explicitly
HLSL 2021. `CompilerRoute::hlslShaderModels` reports the supported model.
DXC supports exactly Vulkan 1.0/SPIR-V 1.0 and Vulkan 1.1/SPIR-V 1.3 here,
not glslang's additional Vulkan 1.1/SPIR-V 1.0 pair. DXIL routes have an empty
`vulkanTargets` span because there is no intermediate; nondefault Vulkan
settings are rejected. `Compilation::vulkanTarget` is meaningful only on
SPIR-V/intermediate routes. shaderc exposes the same contract with
`--source-language hlsl --hlsl-shader-model 6.0 --entry FUNCTION`.

Includes, guards, conditional sections and macros use DXC's own preprocessor.
DXC's include callback supplies the **searched candidate path**, not the original
directive's parent or quote/angle classification. Accordingly the shared
resolver receives that candidate, an empty `including`, and `system=false`;
without a callback it reads the candidate file. Native relative/`-I` searches
remain authoritative. A resolver must understand the provider's candidate-path
namespace and return stable identities. Each attempt preprocesses current
dependencies before cache lookup, then compiles against the frozen snapshot.
Conflicting content for one identity is an error. Ordered roots, normalized
defines, entry/stage/profile/debug settings, provider pins, effective target,
source identities/content and preprocessed text all contribute to cache identity.
Failed preprocessing, compilation, validation or reflection cannot publish a
package or replace `RebuildState::last_good()`.

SPIR-V uses GL-compatible buffer layout, real SPIRV-Tools validation and the
same canonical emitted-binary reflection as GLSL. DXIL instead uses DXC's own
in-process validator, `ID3D12ShaderReflection`, and upstream checked PSV
metadata for exact interpolation. It is **not** reflected by separately
compiling the source to SPIR-V: target-predefined macros can change resources.
Tests exercise that distinction and reject deliberately corrupted DXIL.
Native D3D12 execution/Windows driver acceptance is not established by the
open-source validator on this Mac.

DXIL's bounded canonical contract requires `TEXCOORDn` for arbitrary user IO
(including vertex inputs, matching the current D3D12 backend); fragment
`SV_Targetn` maps to color locations and built-ins stay native. Scalar/vector
32-bit leaves and fixed descriptor arrays are represented. Unbounded arrays,
typed buffer resources and counter/append/consume resources are rejected.
Different HLSL register classes must use unique logical binding indices within
a space; no implicit remapping merges `b0`, `t0`, and `s0`. DXIL uses native
HLSL buffer layout, so authors must ensure compatible layouts when combining
targets in one package; agreement checks are never bypassed. Vulkan-specific
push/specialization attributes do not invent DXIL runtime-specialization or
push-constant support. Unsupported reflected constructs fail explicitly.

Evidence: all nine SPIR-V/DXIL stage/configuration combinations compile, validate,
reflect, cache and produce byte-identical CLI/library packages; 14 additional
HLSL portable combinations compile and validate. The same serialized generated
HLSL graphics packages yield exactly 256 RGBA `(64,128,255,255)` pixels on
Vulkan/MoltenVK, Metal and OpenGL 4.1. Vulkan and Metal compute write exactly
`107..110`, leaving four sentinels untouched. Native validation is enabled.
Compiler-off installed Shader/Renderer consumers have no optional compiler
link dependencies. Shader completion and other-host evidence remain tracked
by #192/#193; this route does not resume Renderer feature work.

### Portable native generation (#187)

`CompileRequest::metalTarget = {MetalVersion::v2_1}`,
`glslTarget = {GlslVersion::v410}`, and
`glslEsTarget = {GlslEsVersion::v300}` are the exact supported native profiles,
and their defaults. No arbitrary upstream target/version/stage is advertised.
Set `request.target` to `metal_source`, `glsl`, or `glsl_es`. GLSL ES *input*
uses Vulkan GLSL ES 310+ semantics; output ES 300 is a distinct conversion.
MSL is macOS source only, not authoring MSL or producing a Metal library (#188).
GL 410 and ES 300 do not advertise compute.

Generated Metal remains experimental: set
`request.allowExperimentalMetalGeneration = true` to compile, and
`ShaderPackageOptions::allowExperimentalMetalGeneration = true` to load.
Neither compiler discovery nor emission disables the package safety gate.
The RHI package's `entryPoint` is the logical selection/agreement identity;
`nativeEntryPoint` records the actual emitted function (for example `main0`).
When authoring a descriptor, an empty native name means the logical spelling;
canonical packages store it explicitly. `Device::create_shader(package, ...)`
selects by logical name and forwards the native spelling. Serialized packages
are loaded and consumed **without test-side metadata repairs**.

The translator reuses emitted-SPIR-V canonical reflection, including full
directional IO, scalar default bits, push ranges and workgroup axis IDs.
Diagnostic resource names may change per target, but logical slots/types and
entry identities still receive cross-target agreement checks. Both separate
texture and sampler records survive GL combined sampling: both name the real
combined uniform and map to its real texture unit. A sampler is never silently
dropped or assigned an unrelated resource.

Native-generation limits, distinct from SPIR-V reflection:

- Descriptor resources must be scalar (no descriptor arrays yet). Metal uses
  explicit flat buffers 16..29 (0..15 reserved for RHI vertex streams, 30 for
  push data), textures 0..127 and samplers 0..15, rejecting excess slots.
  Push constants map to buffer 30; function constants preserve logical IDs and
  defaults, including direct workgroup axis specialization.
- Metal runtime storage-buffer tails can be indexed, but querying their
  `.length()` is `unsupported`: SPIRV-Cross requires an auxiliary buffer-size
  table that RHI does not supply. Translation also rejects reported auxiliary
  swizzle, view-mask, dispatch-base, captured-output/patch-output and input
  threadgroup resources. Multiview, capture/tessellation, swizzling and
  compiler-side dynamic-offset helpers are not enabled; dynamic offsets remain
  native bind-group offsets. No helper slot or fake resource remap is published.
- Generated package shaders explicitly carry complete per-stage resource
  reflection into RHI. Metal skips layout-visible resources absent from that
  stage, even with `ShaderStageMask::all`; it does not fall back into reserved
  vertex-stream slots. Low-level/native shaders do not infer completeness from
  empty reflection; their default binding convention remains available.
- GL 410/ES 300 require one-to-one scalar texture/sampler pairing; many-to-one,
  one-to-many, unpaired resources and synthesized samplers are explicit
  `unsupported` results. A broader sampling graph needs a shared multi-use
  native binding contract, not invented remaps.
- GL emits core-profile code without required extensions, storage resources,
  push constants or specialization constants. Unsupported extensions and
  these constructs are rejected explicitly. Resources must be statically used
  so linked native reflection can retain them. Internal IO must be scalar/vector
  leaves; location-derived varying names handle different source-side names.
- GL vertex emission converts Vulkan's 0..1 clip depth to -1..1. No hidden
  Y-flip or language/target profile guessing is applied.

Every native source is owned by the resulting `Compilation`/TSP, not a borrowed
translator buffer. SPIR-V validation and translation run in process, never
through subprocess tools. GL/ES emitted source is additionally parsed/linked
with real glslang at the selected output profile before packaging.

Evidence: all 42 native route/stage/intermediate-version combinations (two input
profiles; three intermediate configurations; MSL three stages and GL/ES two)
pass unit tests and byte-identical CLI/library comparison. Native default
intermediate packages on Apple M3 Pro produce exactly 256 RGBA `(64,64,32,255)`
pixels on Vulkan/MoltenVK, Metal and OpenGL 4.1 using one portable vertex/fragment
fixture with texture, sampler and uniform bindings. Generated Vulkan and Metal
compute consume push data, scalar defaults/overrides and local-size IDs; widths
2/3 write exactly `2107..2110` / `3113..3118`, preserving sentinel tails.
Metal API/GPU validation and Vulkan validation are enabled in that test.
GLES native execution and higher native profiles remain future evidence, not
claims established by successful compiler validation.

The remaining #73 route matrix is **planned, not implemented or advertised**:

| Input / mechanism | Intended output | Tracking / remaining gate |
| --- | --- | --- |
| Native MSL, optional Apple tooling | MSL / offline Metal library | #188; SDK audit, truthful interfaces/dependencies, native readback |
| HLSL, optional DXC | SPIR-V / DXIL where compiler/host supports it | #189; profiles/reflection, native Vulkan and separately tracked Windows evidence |
| Slang, native modules/imports | Supported SPIR-V first; other outputs gated | #190; compiler audit, imported dependencies, native output; experimental Metal/WebGPU not promoted by emission alone |
| WGSL, optional Tint | WGSL input/output and supported conversions determined by compiler audit | #191; actual directions, language semantics, reflection and compiler validation; no WebGPU execution claim |

These are not an all-input/all-output cross-product promise. Complete reflection, validated
cross-target interfaces/remaps/specialization, reproducible optional compiler
packaging and per-route native conformance remain completion gates under #73
(baseline reflection #186 completed; shared behavior #192, final evidence audit #193).
The completed #178-#181 foundations are not full Shader-module completion.

### Native MSL source and libraries (#188)

Native MSL authoring is a separate route from portable GLSL-to-MSL translation.
It is disabled by default and adds no mandatory compiler dependency to Shader,
RHI, Renderer or prepared-package consumers:

| CMake option | Capability |
| --- | --- |
| `TRUFFLE_SHADER_ENABLE_METAL_COMPILER=ON` | Optional in-process Metal source compilation and reflection. Requires Apple frameworks, a Metal device, and macOS 26+ at runtime for the reflection API. |
| `TRUFFLE_SHADER_ENABLE_METALLIB_COMPILER=ON` | Adds offline `metal`/`metallib` output. Requires the first option and the installed Apple Metal Toolchain (`xcrun --find metal` and `xcrun --find metallib`). |

`Compiler::routes()` reports native source only when the runtime Metal/reflection
capability exists; it reports the separate `metallib` target only when the
offline tools are also executable. An SDK alone does not advertise an offline
route. The current supported profile is macOS Metal 2.1, with vertex,
fragment and compute entry points. Offline AIR is compiled for
`air64-apple-macos13.0`; successful compilation is not a minimum-runtime claim
for every library feature. No toolchain or SDK is fetched or redistributed.

Every native request requires a typed `CompileRequest::interfaceDeclaration`
(`rhi::ShaderReflection`). Fields exposed by `MTLFunction` and
`MTLFunctionReflection` are checked against the selected entry point:

- entry-point function stage;
- active vertex input attribute names, locations and scalar/vector types when
  Metal reports them;
- reflected resource names, kind, access, array count and native index, with
  remaps to the declared logical group/binding;
- declared buffer minimum size against Metal's reflected data size, and push
  ranges against the reserved `buffer(30)` size;
- function-constant ID, name, type and required status.

Metal does not expose logical group/binding, general fragment inputs, stage
outputs, varying locations/interpolation, push-range layout, function-constant
default bits, or the complete workgroup contract through this source reflection
path. Those values remain explicit caller assertions in the typed declaration;
the interface is still serialized through existing TSP reflection fields. On
the current Apple runtime, fragment `stageInputAttributes` is absent, so those
inputs are caller assertions as well. The adapter does not parse MSL to infer
metadata. Compiler-visible changes that contradict observable declaration
fields fail rather than producing a package.

The initially supported resources are named scalar buffers, 2D float textures
and scalar samplers. Buffer indices 0..15 remain reserved for vertex streams,
16..29 are remapped resources, and 30 is reserved for push data. Texture arrays
are accepted only when reflected consistently; sampler arrays, argument
buffers, unsupported texture forms, unrepresented Metal resource kinds and
resources that cannot be remapped fail explicitly. Function constants must be
required in MSL; their typed default bits come from the declaration and RHI
applies them unless a pipeline supplies an override. Unsupported or
unobservable resource forms are never represented by fabricated reflection.

Reusable source composition is an explicit ordered list, not a module language:

```cpp
request.language = truffle::rhi::ShaderSourceLanguage::msl;
request.target = truffle::rhi::ShaderTarget::metal_source; // or metal_library
request.stage = truffle::rhi::ShaderStage::vertex;
request.entryPoint = "vertex_main";
request.sourceUnits = {"shared/types.metal", "shared/lighting.metal"};
request.interfaceDeclaration = typedReflection;
request.allowExperimentalMetalGeneration = true;
```

Shader resolves each requested source unit through the same resolver/include
roots on every attempt, in list order before the root. The root entry point
connects the selected functions. Exact `#line` boundaries preserve source
diagnostics; normalized defines are passed as source preamble and Metal owns
macro/language processing. Unit order, requested/resolved identities, full
contents, defines, include-root order, target profile, interface declaration
and compiler identity/version contribute to the cache key and dependency
records. A failed compile never replaces the last-good rebuild result.
Nonempty `sourceUnits` and typed declarations are explicitly rejected by
routes that do not implement them.

Only the built-in `#include <metal_stdlib>` is accepted. Arbitrary user
`#include`/`#import` resolution is not promised because the in-process Metal
source API has no Shader `SourceResolver` callback. Compose reusable project
code with `sourceUnits`; Metal performs the ordinary MSL preprocessor work
inside each unit. This first route does not recursively resolve nested
includes.

`truffle-shaderc --source-unit FILE` is repeatable and uses the same ordered
request field; `--interface-from PACKAGE` selects the exact entry/stage
declaration (or the sole variant for that stage). `--target msl` emits source;
`--target metallib` invokes the optional offline route. Both require
`--allow-experimental-metal`, and both are validated in-process before
packaging. In compiler-disabled/non-Apple builds, Shader and prepared TSP
consumers remain compiler-free.

Local validation used macOS 27.0.1 / Xcode 27.2, Apple Metal compiler
32023.922 (`metalfe-32023.922.4`), and the installed matching `metallib` tool.
On an Apple M3 Pro with Metal API/GPU validation enabled, composed source and
offline-library packages each read exactly 256 RGBA `(64,64,32,255)` graphics
pixels and compute values `10,12,14,16` (declared specialization default) /
`15,18,21,24` (pipeline override). These are local native results, not
cross-machine or older-OS guarantees.

## On-demand compilation and reload
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
pinned source SHA plus `truffle-glslang=5;vulkan=VERSION;spirv=VERSION`,
target, debug and experimental settings. When enabled it also records the exact
SPIRV-Tools/Headers pins, and native outputs record the SPIRV-Cross pin plus
adapter revision, output profiles, flat slot policy and clip conversion.
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
| Fixed descriptors | Single-dimensional uniform/storage buffer, separate image/texture and sampler arrays; minimum size is **per buffer element**. Nested descriptor arrays are rejected: Vulkan does not permit the emitted nested handle-array type (VUID 04655), even though glslang accepts that source. Arrays **inside** a buffer remain supported. |
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

# Truffle Shader

`truffle_shader` / `Truffle::Shader` owns CPU-side source resolution,
compilation, reflection, compilation caching, and rebuild state. Its public API
is `truffle/shader/compiler.hpp`, namespace `truffle::shader`. It depends only
on RHI/Base and an explicitly enabled private compiler adapter. Renderer owns
GPU objects and safe publication (`renderer::SourceProgram` consumes
`Compilation` results in memory); this module has no backend, Renderer, EnCom,
watcher, application, or tool dependency.

The [standalone Shader workflow example](../../examples/shader_workflow/README.md)
builds directly against `Truffle::Shader` and demonstrates modular GLSL,
requested configurations, cache reuse, rejected edits and last-good recovery.

The accepted module direction is Renderer → Shader → RHI → Base. There is no
adapter/integration module between Renderer and Shader. The core Shader target,
public types, configuration, dependency records, cache ownership, and rebuild
state build and link with every compiler adapter disabled; source compilation
then reports `unsupported`.

Enable `TRUFFLE_SHADER_ENABLE_GLSLANG=ON` to use the pinned GLSL/GLSL ES to
Vulkan SPIR-V adapter. This does **not** require building shaderc.
Canonical SPIR-V reflection also requires pinned `vendor/spirv-headers`, even
when optional SPIRV-Tools validation is disabled; its license notices are installed.
With every compiler adapter off, `Compiler::routes()` is empty, `available()` is false and `compile()` returns
`unsupported`; prepared Truffle Shader Packages (TSP, `.tsp`) still work through
RHI and Renderer without linking a compiler. No language/compiler is fetched.
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
`TRUFFLE_SHADER_ENABLE_SLANG=ON` independently enables native Slang
modules/imports to validated Vulkan 1.1 / SPIR-V 1.3, without glslang.
See [native Slang configuration, pins, limits and tests](../../docs/shader-slang.md).

`TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON` adds real in-process SPIR-V validation.
`TRUFFLE_SHADER_ENABLE_SPIRV_CROSS=ON` requires a SPIR-V source adapter and that validator,
and adds portable Metal/GLSL output. All are private, optional dependencies.
Prepare sources explicitly before configuring (configure never downloads):

```sh
git submodule update --init vendor/glslang vendor/spirv-tools \
  vendor/spirv-headers vendor/spirv-cross
```

Compiler-enabled configure requires clean checked-out sources at the documented
pins, so compiler provenance cannot silently describe different source.
Without Tint, SPIRV-Tools' validation library alone is built/exported; Tint's
SPIR-V reader additionally requires its optimizer library. Compiler CLI
libraries are not part of Shader's install closure. SPIRV-Cross installs its
core/GLSL/MSL static closure and CMake configs. Enabled installs include the
new dependencies' license notices; all-off consumers require none of them.

## Shared composition, cache and rebuild contract (#192)

Shader compiles requested entries/configurations, not a Cartesian product of
every possible feature. Composition follows the selected language:

| Input | Reusable source and configuration | Dependency boundary |
| --- | --- | --- |
| GLSL / GLSL ES | Native includes, guards and defines | Root plus transitive native-preprocessor snapshots |
| HLSL | DXC includes, guards and defines | Root plus native searched include candidates, frozen for compilation |
| Slang | Native imports/includes and component linking, with defines | Root plus native module/include snapshots |
| WGSL | Host-assembled standard declarations/functions and literal constants | One exact-byte source; invented includes/macros and unsupported pipeline overrides are rejected |
| Native MSL | Ordered `sourceUnits` followed by the root, with Metal-owned macros | Every selected unit/root; offline native dependency scanning precedes cache lookup |
| SPIR-V import to WGSL | Select an entry from an existing binary module | Exact input-module bytes, validated before translation |

The host supplies the current root `Source` on each attempt. `read_source()`
provides canonical file identity but starts no watcher. Resolved dependencies
are refreshed before cache lookup; configuration, effective target, source
identity/content and compiler/adapter provenance distinguish relevant results.
Only unchanged effective inputs can reuse a cached package. Unrelated settings
for a different language/target do not imply extra compilation.

Each `Compiler` owns its unbounded in-memory cache until `clear_cache()` or
destruction; no persistent store or global polling service is introduced.
Use one `RebuildState` per logical shader. Compilation, reflection and canonical
package-validation failures return failure, never a successful cached fallback,
and never replace `last_good()`. Clearing/destroying the compiler does not
invalidate the rebuild state's owned last-good package. Results likewise own
their TSP bytes, dependencies, diagnostics and canonical reflection.

shaderc and runtime hosts call the same library mechanisms. CLI compilation
and package-validation failures occur before opening an existing output file;
this is not a general filesystem transaction or crash-recovery guarantee.
Shader owns no GPU residency or publication: Renderer/RHI consumption and
in-flight replacement remain separate.

The standalone example demonstrates transitive composition, explicit
configurations, repeat-cache behavior and failed-edit recovery. Focused native
rebuild tests additionally reject observable reflection mismatches and invalid
canonical push ranges, verify repeated failures never become cache hits, and
load the last-good TSP after cache clearing and compiler destruction.

## Routes and target configuration (#73, #185)

`Compiler::routes()` describes source routes available in this process/runtime,
not package assembly, all features of an upstream compiler, request success, or
eventual GPU execution. Most routes are build-time capabilities. Native MSL is
the narrow exception: it is advertised only when a Metal device is available
and the macOS 26+ reflection API can be used.
Each route lists its accepted language, emitted target, supported stages, and
exact supported Vulkan/SPIR-V configurations; every listed stage/configuration
combination is supported, subject to the reflection limits below.
Native routes also list typed `metalTargets`, `glslTargets`, or `glslEsTargets`.
The Vulkan settings on these routes configure the **intermediate**, not the
native output profile.
Routes are cached on first discovery and their borrowed read-only spans have
process lifetime; native MSL capability changes are not observed afterward.
`available()` is the convenience test for any discovered source route.
`CompilerRoute::experimental` marks routes whose generated Metal/WGSL output
requires explicit package safety consent.

| Shader source acceptance | Emitted output | Enabled dependency | Status / verification |
| --- | --- | --- | --- |
| GLSL (core profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; all reported configurations compile and emitted SPIR-V versions are checked |
| GLSL ES (ES profile), vertex/fragment/compute | Vulkan SPIR-V | pinned glslang | Implemented; same configuration tests |
| GLSL or GLSL ES, vertex/fragment/compute | macOS MSL 2.1 source | glslang + SPIRV-Tools + SPIRV-Cross | Experimental explicit opt-in; generated graphics and compute execute on Apple M3 Pro |
| GLSL or GLSL ES, vertex/fragment | GLSL 410 core (macOS OpenGL 4.1) | same three optional adapters | Generated resource-backed graphics executes on macOS 4.1; compute rejected |
| GLSL or GLSL ES, vertex/fragment | GLSL ES 300 | same three optional adapters | Emission and real glslang parse/link validation; **no native GLES execution evidence** |
| Slang, native modules/imports, vertex/fragment/compute | Vulkan 1.1 / SPIR-V 1.3 | pinned Slang + SPIRV-Tools | Native source/module/entry composition; only requested entry/configuration; see #190 documentation |
| HLSL 2021, shader model 6.0, vertex/fragment/compute | Vulkan SPIR-V or DXIL | pinned DXC + SPIRV-Tools | In-process compile/validation and emitted-target reflection; Vulkan/MoltenVK graphics and compute execute on Apple M3 Pro |
| HLSL, vertex/fragment/compute | Experimental macOS MSL 2.1 source | DXC + the portable adapters above | Generated graphics and compute execute on Apple M3 Pro |
| HLSL, vertex/fragment | GLSL 410 core / GLSL ES 300 | same portable adapters | GL graphics executes on macOS 4.1; ES has real compiler validation, no native evidence |
| WGSL, vertex/fragment/compute | Vulkan 1.1 / SPIR-V 1.3 | Tint + SPIRV-Tools | Real compiler validation and canonical reflection; native resource-backed graphics and compute exact readback on Vulkan/MoltenVK |
| WGSL, vertex/fragment/compute | WGSL | Tint + SPIRV-Tools | Selected-entry IR emission, experimental opt-in, emitted WGSL reparsed by Tint and recompiled in tests |
| Vulkan SPIR-V, vertex/fragment/compute | WGSL | Tint + SPIRV-Tools (including optimizer) | Real upstream reader, selected-entry emission and validation; bounded by Tint's representable input subset |
| WGSL, vertex/fragment/compute | macOS MSL 2.1 | Tint + SPIRV-Tools + SPIRV-Cross | Experimental opt-in; native resource-backed graphics and compute exact readback on Metal |
| MSL, vertex/fragment/compute | macOS MSL 2.1 source | optional Apple Metal framework adapter | #188; requires macOS 26+ runtime reflection API and Metal device; exact M3 Pro graphics/compute readback |
| MSL, vertex/fragment/compute | macOS metallib | optional Apple Metal framework adapter plus installed Metal Toolchain | #188; actual `metal`/`metallib` compile and exact M3 Pro graphics/compute readback |
| Slang, vertex/fragment/compute | Vulkan 1.1 / SPIR-V 1.3 | pinned in-process Slang + SPIRV-Tools | Implemented in PR #200; native Vulkan/MoltenVK graphics and compute execute on Apple M3 Pro |
| Backend-native source | No Shader source-compilation route | none needed for assembly | Prepared TSP inputs, **not** source-compilation routes |

### WGSL and Tint (#191)

Tint is independent of glslang. Its source lives in the pinned Dawn tree, but
Truffle builds no Dawn WebGPU implementation, GUI/window system, browser, test,
benchmark, language-server or compiler executable. Prepare sources explicitly:

```sh
git submodule update --init vendor/dawn vendor/abseil \
  vendor/spirv-tools vendor/spirv-headers
cmake -S . -B build-wgsl -DTRUFFLE_BUILD_SHADERC=ON \
  -DTRUFFLE_SHADER_ENABLE_TINT=ON -DTRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON
cmake --build build-wgsl --target truffle-shaderc --parallel 2
```

WGSL is an existing shader language, not a Truffle composition language.
The host can compose standard WGSL declarations/functions into `Source::text`
and supply one stable `Source::identity`; its exact assembled bytes are the
single dependency. Truffle invents no include/import/preprocessor directives.
Nonempty `sourceUnits`, `defines`, `includeRoots`, or `resolver` return typed `unsupported`
before the compiler or resolver runs. Pipeline `override` declarations used by
the selected entry also return `unsupported`, including overrides with defaults:
there is no hidden macro substitution or claim of preserved specialization.
Use literal WGSL `const` configuration in composed source for this bounded
adapter. `generateDebugInfo` is unsupported. Unsupported reflected resources,
types, extensions or intermediate constructs fail with diagnostics rather than
being accepted without metadata.

Set `request.language = ShaderSourceLanguage::wgsl`. Stage and entry name must
match a real WGSL entry point; all other entry points are removed by Tint's
native IR transform. This pin emits SPIR-V 1.3 only, so every Tint route reports
only `{VulkanVersion::v1_1, SpirvVersion::v1_3}` (the default). Tint's robustness
and workgroup initialization defaults remain enabled. Automatic vertex
`PointSize` emission is disabled consistently, avoiding an undeclared point-size
output in triangle MSL pipelines. There is no new point-topology guarantee.
SPIRV-Tools validates every intermediate before the shared canonical RHI
reflection reader processes it; no independent WGSL resource/IO metadata model
is introduced.

For WGSL output use `request.target = ShaderTarget::wgsl` and
`request.allowExperimentalWgslGeneration = true`; loading likewise requires
`ShaderPackageOptions::allowExperimentalWgslGeneration = true`. Both writers
receive separate lowered IR instances because Tint writers mutate their input.
Generated WGSL is reparsed by Tint before a package is returned. SPIR-V input
uses `ShaderSourceLanguage::spirv`, target `wgsl`, and the binary module's bytes
in `Source::text` (including NULs); `read_source()` supports binary reads.
This is the supported path from existing intermediates, not an advertised
direct GLSL/HLSL/Slang-to-WGSL route. Tint may reject valid SPIR-V outside its
WGSL-representable subset; such input is diagnosed. This includes no universal
SPIR-V or arbitrary Vulkan extension compatibility promise.

```sh
truffle-shaderc --compile --name example --source-language wgsl --target spirv \
  --stage compute --entry main --input example.wgsl --output example.tsp
truffle-shaderc --compile --name example --source-language spirv --target wgsl \
  --stage compute --entry main --input example.spv --output example-wgsl.tsp \
  --allow-experimental-wgsl
truffle-shaderc --inspect example-wgsl.tsp --allow-experimental-wgsl
```

Compiler revision, dependency pins, effective target, source identity/bytes,
stage/entry and opt-ins distinguish cache entries. Failures never enter the
cache or replace `RebuildState::last_good()`, and failed CLI compilation or
validation leaves existing package files untouched. Enabled installs export only the used private static
Tint/Abseil/Tools closure, with licenses, without exporting Tint headers as a
Truffle API. All adapters off leaves prepared TSP consumers compiler-free.

Focused evidence: WGSL graphics IO, uniform/texture/sampler/storage bindings,
compute size, selected entries, invalid source/stage/entry/options, emitted
WGSL recompile, SPIR-V import, cache/edit/failure/recovery, CLI/library exact
package equivalence, and installed consumers. Native Apple M3 Pro
Vulkan/MoltenVK and Metal (validation enabled) each read back all 256 pixels as
RGBA `(64,64,32,255)` and all four compute words as `(12,13,14,15)`.
This is Shader-output consumption evidence, **not WebGPU backend/runtime
execution or a backend maturity promotion**. TSP remains unreleased draft 1.0.
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
SPIRV-Cross-generated Metal is distinct from native MSL authoring and offline
library compilation (#188).
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

The route adapters have bounded, independently reported contracts. HLSL/DXC
(#189), Slang (#190), and WGSL/Tint (#191) are merged; native MSL (#188) is
the current active phase. The remaining Shader-level audits are:

| Input / mechanism | Intended output | Tracking / remaining gate |
| --- | --- | --- |
| Shared behavior | Composition, cache/rebuild, compiler and CLI consistency | #192; verify across optional routes/providers |
| Completion audit | Route, reflection, package isolation and native evidence | #193; close #73 only when all gates pass |

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

For in-process source output, only the built-in `#include <metal_stdlib>` is
accepted; arbitrary user `#include`/`#import` resolution is not promised because
the Metal source API has no Shader `SourceResolver` callback. Compose reusable
project code with `sourceUnits`. For offline library output, Metal's native
dependency scan runs on every attempt before cache lookup; any active dependency
outside the installed Metal Toolchain is rejected. This catches conditional
includes that become active after a previously cached request. Metal performs
its own preprocessing for both targets; Shader does not parse MSL or claim
recursive user-include support.

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
backend maturity. The remaining Shader-level audits are tracked by #192-#193.

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

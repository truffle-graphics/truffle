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
pinned source SHA plus `truffle-glslang=2;vulkan=VERSION;spirv=VERSION`.
The effective target therefore survives TSP serialization and shaderc inspection
without changing the RHI/TSP schema. Both target options independently
differentiate cache keys, even when generated instructions otherwise match.

## Reflection and limits

The adapter reflects all linked resource declarations, including inactive
ones, using glslang types/layout calculation. Supported resources are explicit
set/binding uniform and storage buffers, separately bound images/textures and
samplers, and fixed one-dimensional image/sampler descriptor arrays. Buffer
minimum sizes come from glslang's block layout; storage access is preserved.
32-bit scalar/vector location-based stage inputs/outputs and literal compute
workgroups are reflected. `Compilation::interfaces` preserves both IO
directions. Because TSP 1.0 has no IO direction field, its interface list
contains vertex inputs and fragment outputs (pipeline edges); linked varyings
remain in the compilation result. Binary schema and C++ package type do not
change.

Unsupported language/target/stage, non-`main` GLSL entry points, combined
image-samplers, subpass inputs, push constants, specialization constants
(including specialized workgroup sizes), buffer descriptor arrays, unsized
descriptor arrays, and complex/matrix/array/component-packed/indexed IO fail explicitly.
No fake empty reflection is substituted. Other compiler routes remain #73
followups. No native backend maturity is claimed by this CPU compilation API.

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

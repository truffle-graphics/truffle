# Native Slang modules (#190)

`TRUFFLE_SHADER_ENABLE_SLANG=ON` enables the optional in-process Slang adapter
owned by `truffle_shader`. `truffle-shaderc` is a client of the same API.
There is no Shader dependency on a backend or Renderer, and no new reflection
model or package version: output uses canonical RHI reflection and TSP 1.0.

## Implemented route

| Source | Stages | Output | Validation |
|---|---|---|---|
| Slang 2026.12.1 | vertex, fragment, compute | SPIR-V 1.3, Vulkan 1.1 | pinned in-process SPIRV-Tools, canonical emitted-SPIR-V reflection |

This is the actual implemented matrix, not a list of upstream Slang targets.
No Slang Metal, WebGPU, GLSL, DXIL, advanced stage, or backend maturity claim is
made. Unsupported reflection constructs fail rather than losing metadata.
GLSL does not need Slang; Slang does not need glslang or SPIRV-Cross.
`DrawParameters` is explicitly rejected by canonical SPIR-V reflection because
RHI has no device-feature contract for it. In Slang, use native
`SV_VulkanVertexID` / `SV_VulkanInstanceID` when direct Vulkan vertex/instance
indices are intended. `SV_VertexID` / `SV_InstanceID` subtract the base index
and therefore require that unsupported capability; no backend feature is
silently enabled or source semantics rewritten.

Slang owns `import`, `#include`, module lookup, source diagnostics, cyclic import
detection, and component linking. Truffle calls `loadModuleFromSourceString`,
`findAndCheckEntryPoint`, `createCompositeComponentType`, `link`, and
`getEntryPointCode`. Exactly one requested entry point is linked/emitted.
Session-wide `CompileRequest::defines` select one configuration; Truffle does
not enumerate permutations. Other entry points in a module are not emitted.
Slang still semantically checks imported modules (including their declarations);
this is not a promise to ignore syntax errors in unused declarations.

Use native imports to compose reusable source modules, not `sourceUnits`
(which is reserved for native MSL and explicitly rejected here). This adapter
supports source modules, not pre-serialized `.slang-module` artifacts, which
could obscure their original source dependency closure. Generic/extern
specialization APIs are not exposed; ordinary language-level composition and
macro configurations remain available.

## Offline source build (default)

Acquisition is explicit. Configure and build never download dependencies:

```sh
git submodule update --init vendor/slang vendor/spirv-tools vendor/spirv-headers
git -C vendor/slang submodule update --init external/miniz external/lz4 \
  external/unordered_dense external/spirv-headers external/vulkan external/lua \
  external/cmark external/fast_float
cmake -S . -B build-slang -DTRUFFLE_SHADER_ENABLE_SLANG=ON \
  -DTRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON -DTRUFFLE_BUILD_SHADERC=ON
cmake --build build-slang --target truffle-shaderc --parallel 2
```

The adapter builds the clean pinned source closure.
It disables upstream prebuilt downloads, LLVM, DXIL/DXC, Slang RHI,
glslang, Dawn/Tint, CUDA, graphics tools, tests, and examples. Direct SPIR-V
emission requires none of those runtime plugins. The core module and its source
are embedded. The compiler runtime is shared; Truffle's source-built install
exports only that required runtime, not the upstream SDK, tools or plugins.
Its required static compression dependencies are internal to that dylib.

| Source pin | License / role |
|---|---|
| Slang `90b8e77eafdfa0aabbbc48f0e86f6cfbde4cb8f8` (`v2026.12.1`) | Apache-2.0 WITH LLVM-exception; upstream `LICENSES` notices retained |
| miniz `6ef6c68f4fcbb8287aa8edf9c6670804932f41c6` | MIT; compiler archive compression |
| LZ4 `7f71be01f2c6f6b2c3cddb67eef8c38eab12ffd4` | library BSD-2-Clause; tools' GPL license is not the linked library license |
| unordered_dense `73f3cbb237e84d483afafc743f1f14ec53e12314` | MIT; compiler containers |
| Lua `3fe7be956f23385aa1950dc31e2f25127ccfc0ea` | MIT notice in `lua.h`; build-time source generator only |
| cmark `924936d0427cb25a61169739a7660230bffa6ea6` | BSD-2-Clause with embedded notices in `COPYING`; compiler documentation parsing |
| fast_float `e0b53eaf63c6d00e0725788ef1dbb759aa321d79` | MIT / Apache-2.0 / Boost-1.0 alternatives; numeric parsing |
| Slang SPIRV-Headers `c63848ecf2200425511319fd8bf2c17b751e501e` | Khronos notices; Slang's own grammar generation |
| Slang Vulkan-Headers `39f924b810e561fd86b2558b6711ca68d4363f68` | upstream Apache/MIT notices; source build headers, not a Shader backend dependency |

Slang's grammar headers are separate from Truffle's independently pinned
SPIRV-Tools/Headers validation pair. Do not silently substitute one pin for the
other. There are no local upstream patches. Updating requires reviewing APIs,
source and third-party licenses, exact pins, runtime version, the focused
tests below, and native execution; advancing a release tag alone is insufficient.

Native host evidence is Apple Silicon macOS; Windows/Linux source build support
is upstream capability, not local evidence. No binary SDK or system fallback is
implemented. The initially audited 2025.10.1 release was rejected because it
unconditionally probed an optional downstream optimizer, even for direct
unoptimized SPIR-V. The selected clean upstream release contains fix
`fcfaa5f96eaaa0731b00348497146a922743508b` (#11797), so this route can disable
downstream linking/optimization and validate independently without loading
glslang. No local upstream patch is required.

## API, source identities and rebuilding

```cpp
truffle::shader::CompileRequest request;
request.name = "configured";
request.source = truffle::shader::read_source("main.slang").value();
request.language = truffle::rhi::ShaderSourceLanguage::slang;
request.stage = truffle::rhi::ShaderStage::compute;
request.entryPoint = "computeMain";
request.includeRoots = {"shaders"};
request.defines = {{"QUALITY", "3"}};
truffle::shader::Compiler compiler;
auto result = compiler.compile(request);
```

The equivalent CLI request is:

```sh
truffle-shaderc --compile --name configured --source-language slang \
  --target spirv --stage compute --entry computeMain --define QUALITY=3 \
  --include-root shaders --input main.slang --output configured.tsp
```

Imports/includes are read through a Slang `ISlangFileSystemExt` backed by the
existing `SourceResolver`, or canonical filesystem reads when no callback is
provided. Slang combines import/include paths; callbacks receive the requested
path and originating file/search root. They must return stable, unique source
identities with consistent content. Search roots are not a security sandbox.
The native filesystem API does not expose local/system include classification,
so Slang resolver callbacks receive `system=false`.
The filesystem refuses conflicting content for one identity during an attempt.
For arbitrary root identities, the internal module path has a `.slang` suffix
so Slang does not infer a different language from a `.vert`/`.frag` suffix;
dependency identity remains exactly the caller's identity. Native diagnostics
may display that internal suffix. Import paths still follow native Slang rules.

Each attempt has a fresh native session and immutable source snapshot.
Transitive source identities/content hashes, sorted defines, ordered search
roots, root identity, entry, stage, compiler/source/provider identity, debug
setting, and validation pins enter cache/provenance. A cache hit skips code
generation only after native module resolution/linking has established the
current dependency closure; it never trusts stale timestamps or a previous
compiler session's import cache. Missing imports and native diagnostics
therefore cannot return an earlier success. Failed attempts never enter cache;
`RebuildState::last_good()` remains separate and unchanged after failure.
Instances remain single-thread-owned; adapter entry is conservatively serialized.

## Focused evidence

`truffle_shader_slang_tests` covers native multilevel imports through a virtual
resolver, content/configuration/debug invalidation, canonical resource and
workgroup reflection, one emitted entry, all three stages, duplicate defines,
missing/cyclic imports, stage mismatch, concurrent compiler instances, and
failed-rebuild preservation/recovery.
`truffle_shaderc_slang_modules` compares CLI/library package bytes exactly and
checks failed CLI compilation preserves its previous output.
`truffle_shader_slang_vulkan_tests` executes vertex/fragment code with 256 exact
RGBA(78,0,0,255) pixels, then imported compute code for `QUALITY=3/4`, checking
eight exact output words and an untouched sentinel.
No Renderer feature work is involved. Compiler-off installed Shader/Renderer/RHI
consumers must still resolve without compiler libraries/package requirements.
After normally merging the HLSL baseline, focused combined GLSL+Slang checks
pass 15/15, Slang-only checks 9/9, and compiler-off checks 10/10. HLSL request/
route fields and native empty-intermediate listing are retained; revalidation
keeps DXC disabled rather than creating another DXC source build. Slang reuses
the shared define normalization and length-delimited cache-key input helpers.

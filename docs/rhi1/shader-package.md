# RHI 1 Truffle Shader Package Contract

Truffle Shader Package (TSP, `.tsp`), C++ `truffle::rhi::ShaderPackage`, is the
format-neutral runtime shader contract. It owns a
canonical manifest plus target blobs and can be created, serialized, loaded,
capability-checked, and selected without a compiler dependency.

The independent `Truffle::Shader` library optionally compiles configured
multi-file GLSL/GLSL ES or HLSL directly into an in-memory Truffle Shader Package
(TSP, `.tsp`). It owns dependency tracking, supported reflection, compilation
caching and explicit rebuild state; Renderer owns GPU object lifetimes and
replacement. See [the Shader API](../../src/shader/README.md).

`truffle-shaderc` is an optional host tool. Its dependency-free path assembles
precompiled or backend-native variants. Its separately enabled pinned glslang
route consumes the shared Shader library to compile GLSL and GLSL ES to SPIR-V.
The independent pinned DXC route compiles HLSL 2021/SM 6.0 to SPIR-V and DXIL,
with in-process output validation and native emitted-binary reflection.
Portable outputs additionally use pinned optional SPIRV-Cross and real
SPIRV-Tools validation inside Shader, producing macOS MSL 2.1, desktop GLSL
410 core, or GLSL ES 300 source. These paths record authoring-language
provenance, source SHA-256 values, and compiler identity. Generated modules
default to Vulkan 1.1 and SPIR-V 1.3, matching the current native Vulkan lane.
Explicit Vulkan 1.0/SPIR-V 1.0 and Vulkan 1.1/SPIR-V 1.0 configurations are
also supported for GLSL; DXC supports the first pair but not Vulkan 1.1/SPIR-V 1.0.
Additional configuration compilation evidence is not a new native
backend claim. The tool never fetches or discovers a compiler
implicitly. Additional source-to-target compiler adapters must arrive with
their pinned submodules and explicit enable flags.

## Inputs And Variants

The package can record Slang, HLSL, GLSL, GLSL ES, WGSL, and MSL authoring
provenance; Shader compiles GLSL/GLSL ES/HLSL through Vulkan SPIR-V to the bounded
native profiles above when their adapters are explicitly enabled.
See the [implemented versus planned route matrix](../../src/shader/README.md#routes-and-target-configuration-73-185).
Accepted prepared inputs are SPIR-V, DXIL, Metal libraries, and
backend-native source variants.

The DXIL target may carry HLSL source only as a `native_override`. A Direct3D
backend with its optional runtime compiler route may compile that override;
precompiled and generated DXIL variants remain binary bytecode.

A package may carry several target variants at once: SPIR-V, DXIL, MSL or
`metallib`, WGSL, desktop GLSL, and GLSL ES. Selection is deterministic: a
native override wins over a precompiled variant, which wins over a generated
variant for the same target, stage, entry point, and permutation.

Every package records:

- entry points and stages;
- logical entry identity plus the exact native entry spelling;
- permutations, defines, and specialization constants;
- required RHI capabilities;
- logical binding layouts and per-target remapping;
- full directional vertex/fragment interfaces, including internal varyings;
- normalized reflection data and source hashes;
- compiler identities, versions, and revisions;
- bounded diagnostics and provenance for every variant.

Reflection for variants with the same entry point, stage, and permutation must
normalize to the same logical contract. Package creation fails if resource
types, arrays, access, stage visibility, push-constant ranges, specialization
constants, interfaces, or workgroup metadata do not agree.
Diagnostic binding, constant and interface names are preserved but do not
determine agreement; logical slots, scalar IDs/types/default bits and occupied
stage/direction/locations/types/interpolation and workgroup axis references do. Conventional 32-bit `floatN`/`intN`/`uintN`
and `vecN`/`ivecN`/`uvecN` leaves are equivalent for N=2..4; empty/default,
`perspective` and explicit `smooth` interpolation agree, including reordered
known centroid/sample qualifiers. These comparisons
preserve original metadata in serialization; unknown spellings are not guessed.
A spelling difference must not hide an actual semantic mismatch.

Shader's baseline reflection work (#186) populates the existing push ranges,
scalar specialization records, fixed descriptor counts, per-buffer minimum
addressable sizes/access and expanded 32-bit scalar/vector IO leaves directly
from emitted SPIR-V. A buffer minimum does not include unused trailing alignment
padding. Shaderc's inspection prints that metadata rather than re-reflecting
source. See [the feature matrix and blockers](../../src/shader/README.md#reflection-and-limits).
Canonical IO direction lives on `ShaderInterfaceVariable`; unique keys are
`(stage,direction,location)`. Both directions at one location round-trip.
`Compilation::interfaces()` borrows package records without duplicate storage;
selected `ShaderDesc` carries those records for prepared-package stage-link and
vertex-attribute checks. Workgroup metadata includes optional per-axis scalar
IDs alongside default dimensions. RHI resolves typed overrides before explicit
size matching, axis/total limits, native creation and retained dispatch metadata.
Direct positive int32/uint32 references are supported; expression-derived
dimensions fail explicitly rather than using stale defaults.

The version 1.0 binary layout is defined in
[the TSP format specification](shader-package-format.md). It uses a
fixed little-endian header, a canonical manifest, ordered target blobs, and
per-section integrity hashes. Load rejects incompatible versions, invalid enum
values, excessive counts or sizes, corrupt hashes, non-canonical ordering, and
trailing data.
This unreleased draft intentionally retains identifiers `1.0` while contracts,
codec and consumers evolve together. Earlier development artifacts must be
rebuilt; there are no legacy readers or migrations. Stable format/versioning
guarantees begin with release, per the linked specification's compatibility policy.

## Compilation Policy

Prepared packages remain compiler-free. Offline and on-demand runtime source
compilation share the optional Shader library; native backend compilation is a
separate capability-reported route.

Planned #73 routes may use Slang and Tint;
they are not currently integrated or advertised. Pinned glslang/DXC are linked only by explicitly
enabled Shader adapters (`TRUFFLE_SHADER_ENABLE_GLSLANG` / `TRUFFLE_SHADER_ENABLE_DXC`) and never by RHI,
backends or compiler-disabled Renderer consumers. Slang is one planned authoring route, not Truffle's
mandatory shader language. Generated Metal and WGSL variants are rejected by
default and require an explicit experimental package option. The CLI exposes
`--allow-experimental-metal` for generated MSL compilation, assembly, append
and inspection; WGSL remains gated without a CLI opt-in. The compiler request's
Metal opt-in does not implicitly opt future package loads in.

The canonical manifest stores `entryPoint` as the logical selection/agreement
key and `nativeEntryPoint` as the native function spelling. Different emitted
entry names do not evade cross-target reflection agreement. An empty native
name in a freshly authored descriptor means the logical spelling; canonical
packages record both explicitly. Runtime selection uses the logical spelling
and forwards the serialized native name to the backend without patching blobs.
This evolving TSP draft retains `1.0`, without legacy readers.

Generated variants promise exhaustive per-stage resource reflection. Runtime
selection explicitly marks `ShaderDesc::resourceBindingsComplete`, so Metal
does not bind layout-visible but absent stage resources through native fallback
slots. Precompiled/native-override variants and direct descriptors retain
optional metadata by default; empty reflection alone is not proof of no use.
See [binding semantics](bindings-and-pipelines.md#layout-and-reflection-agreement).
Generated MSL rejects runtime storage-buffer `.length()` and other reported
auxiliary-resource requirements until RHI owns the required bindings; ordinary
runtime-tail indexing remains supported.

## Runtime And Tool Evidence

- Package creation is deterministic after canonical sorting.
- Native target variants override generated variants.
- Cross-target reflection agreement has positive and negative fixtures.
- Source languages, hashes, compiler versions and revisions, diagnostics,
  permutations, remaps, interfaces, and capability requirements round-trip.
- Corrupt, incompatible, capability-mismatched, and non-canonical packages fail
  with typed results.
- `Device::create_shader(package, target, entry, stage, permutation)` consumes a
  selected package variant. The native Metal proof creates MSL shaders and
  pipeline state from that path. The D3D12 proof retains offline DXIL or uses
  the Windows SDK D3DCompiler for explicitly selected HLSL native overrides.
  The Vulkan proof creates an owned `VkShaderModule` from the generated SPIR-V
  package and consumes a reflected compute variant through descriptor-backed
  dispatch with exact storage-buffer readback. Graphics pipeline capability
  remains separately gated.
- `truffle-shaderc` self-test and file-based assemble/inspect smoke tests run in
  CI when `TRUFFLE_BUILD_SHADERC=ON`.
- With `TRUFFLE_SHADER_ENABLE_GLSLANG=ON`, local tests compile a representative shader
  twice to prove deterministic SPIR-V packages and requires invalid GLSL to
  return bounded compiler diagnostics.

Native execution on each additional backend remains its own evidence gate.
Recording a target blob does not by itself advance a backend maturity level.

## Tool Usage

Build the package assembler with `TRUFFLE_BUILD_SHADERC=ON`. Independently
enable GLSL source compilation with `TRUFFLE_SHADER_ENABLE_GLSLANG=ON`, or HLSL
with `TRUFFLE_SHADER_ENABLE_DXC=ON` and `TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON`; the default
core Shader/RHI/Renderer configuration remains compiler-free, and runtime
Shader clients can leave shaderc disabled.

```sh
truffle-shaderc --routes
truffle-shaderc --compile --name material --target spirv --stage fragment \
  --source-language glsl --input shaders/material.frag --output material.tsp \
  --include-root shaders/include --define USE_TEXTURE=1 --define QUALITY=2
truffle-shaderc --inspect material.tsp
truffle-shaderc --compile --name hlsl-material --target dxil --stage fragment \
  --source-language hlsl --input shaders/material.hlsl --output material-dxil.tsp \
  --entry fragmentMain --hlsl-shader-model 6.0 --include-root shaders/include \
  --define USE_TEXTURE=1
truffle-shaderc --compile --name legacy-target --target spirv --stage vertex \
  --source-language glsl --input shaders/triangle.vert --output triangle-vk10.tsp \
  --vulkan-version 1.0 --spirv-version 1.0
truffle-shaderc --compile --name material --target msl --stage fragment \
  --source-language glsl --input shaders/material.frag --output material-metal.tsp \
  --msl-version 2.1 --allow-experimental-metal
truffle-shaderc --inspect material-metal.tsp --allow-experimental-metal
truffle-shaderc --compile --name material --target glsl --stage fragment \
  --source-language glsl --input shaders/material.frag --output material-gl41.tsp \
  --glsl-version 410
truffle-shaderc --compile --name material --target glsl-es --stage fragment \
  --source-language glsl --input shaders/material.frag --output material-es30.tsp \
  --glsl-es-version 300
```

`--routes` reports Shader's enabled source language/target/stage/configuration
support (zero source routes when compilers are disabled), independently of
the assembler's accepted target names. `--vulkan-version` and `--spirv-version`
are compilation-only settings, validated by Shader, defaulting to 1.1/1.3.
Both effective versions are recorded in compiler revision provenance and
reported by `--inspect`. There is no CLI-owned compiler selection policy.
Native version flags are compilation-only, validate the selected target, and
accept exactly the corresponding typed Shader profile. Discovery marks Metal
experimental and lists only graphics stages for GL 410/ES 300.
`--hlsl-shader-model 6.0` is compilation-only and requires HLSL source.
DXIL discovery has no Vulkan intermediate; its profile is selected by shader
model and stage. See the [HLSL reflection and include contract](../../src/shader/README.md#hlsl-and-dxc-189).

Inspection prints native entry names and every binding remap. Append preserves
translator-owned remaps and rejects conflicting global target/stage mappings.
The [native generation limits and exact native evidence](../../src/shader/README.md#portable-native-generation-187)
are narrower than upstream SPIRV-Cross feature coverage. GLES compiler validation
does not claim native GLES execution, nor does this phase complete Shader.

Include roots and defines are repeatable and follow the shared library's
resolution/configuration rules. `--define NAME` means `NAME=1`. Missing
dependencies, malformed/duplicate defines, unsupported reflection and invalid
source produce nonzero exit status and diagnostics. Compilation calculates
real root/transitive source SHA-256 values and the compiler identity; optional
`--source-hash` verifies the root hash rather than supplying false provenance.
Prepared assembly still requires `--source-hash`, `--compiler-name` and
`--compiler-version`. `--append existing.tsp` merges prepared or compiled
variants with matching package name and source provenance. Every configuration
is compiled only when requested, not by enumerating a permutation matrix.

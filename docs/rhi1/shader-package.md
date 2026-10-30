# RHI 1 Truffle Shader Package Contract

Truffle Shader Package (TSP, `.tsp`), C++ `truffle::rhi::ShaderPackage`, is the
format-neutral runtime shader contract. It owns a
canonical manifest plus target blobs and can be created, serialized, loaded,
capability-checked, and selected without a compiler dependency.

The independent `Truffle::Shader` library optionally compiles configured
multi-file GLSL/GLSL ES directly into an in-memory Truffle Shader Package
(TSP, `.tsp`). It owns dependency tracking, supported reflection, compilation
caching and explicit rebuild state; Renderer owns GPU object lifetimes and
replacement. See [the Shader API](../../src/shader/README.md).

`truffle-shaderc` is an optional host tool. Its dependency-free path assembles
precompiled or backend-native variants. Its separately enabled pinned glslang
route consumes the shared Shader library to compile GLSL and GLSL ES to SPIR-V.
Both paths record authoring-language
provenance, source SHA-256 values, and compiler identity. Generated modules
default to Vulkan 1.1 and SPIR-V 1.3, matching the current native Vulkan lane.
Explicit Vulkan 1.0/SPIR-V 1.0 and Vulkan 1.1/SPIR-V 1.0 configurations are
also supported by Shader; their CPU compilation evidence is not a new native
backend claim. The tool never fetches or discovers a compiler
implicitly. Additional source-to-target compiler adapters must arrive with
their pinned submodules and explicit enable flags.

## Inputs And Variants

The package can record Slang, HLSL, GLSL, GLSL ES, WGSL, and MSL authoring
provenance; only GLSL/GLSL ES to Vulkan SPIR-V is currently compiled by Shader.
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
- permutations, defines, and specialization constants;
- required RHI capabilities;
- logical binding layouts and per-target remapping;
- vertex and fragment interfaces;
- normalized reflection data and source hashes;
- compiler identities, versions, and revisions;
- bounded diagnostics and provenance for every variant.

Reflection for variants with the same entry point, stage, and permutation must
normalize to the same logical contract. Package creation fails if resource
types, arrays, access, stage visibility, push-constant ranges, specialization
constants, interfaces, or workgroup metadata do not agree.

The version 1.0 binary layout is defined in
[the TSP format specification](shader-package-format.md). It uses a
fixed little-endian header, a canonical manifest, ordered target blobs, and
per-section integrity hashes. Load rejects incompatible versions, invalid enum
values, excessive counts or sizes, corrupt hashes, non-canonical ordering, and
trailing data.

## Compilation Policy

Prepared packages remain compiler-free. Offline and on-demand runtime source
compilation share the optional Shader library; native backend compilation is a
separate capability-reported route.

Planned #73 routes may use Slang, DXC, SPIR-V Tools, SPIRV-Cross and Tint;
they are not currently integrated or advertised. Pinned glslang is linked only by the explicitly
enabled Shader adapter (`TRUFFLE_SHADER_ENABLE_GLSLANG`) and never by RHI,
backends or compiler-disabled Renderer consumers. Slang is one planned authoring route, not Truffle's
mandatory shader language. Generated Metal and WGSL variants are rejected by
default and require an explicit experimental package option; the CLI does not
expose that opt-in.

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
enable source compilation with `TRUFFLE_SHADER_ENABLE_GLSLANG=ON`; the default
core Shader/RHI/Renderer configuration remains compiler-free, and runtime
Shader clients can leave shaderc disabled.

```sh
truffle-shaderc --routes
truffle-shaderc --compile --name material --target spirv --stage fragment \
  --source-language glsl --input shaders/material.frag --output material.tsp \
  --include-root shaders/include --define USE_TEXTURE=1 --define QUALITY=2
truffle-shaderc --inspect material.tsp
truffle-shaderc --compile --name legacy-target --target spirv --stage vertex \
  --source-language glsl --input shaders/triangle.vert --output triangle-vk10.tsp \
  --vulkan-version 1.0 --spirv-version 1.0
```

`--routes` reports Shader's enabled source language/target/stage/configuration
support (zero source routes when compilers are disabled), independently of
the assembler's accepted target names. `--vulkan-version` and `--spirv-version`
are compilation-only settings, validated by Shader, defaulting to 1.1/1.3.
Both effective versions are recorded in compiler revision provenance and
reported by `--inspect`. There is no CLI-owned compiler selection policy.

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

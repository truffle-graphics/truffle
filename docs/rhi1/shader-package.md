# RHI 1 Shader Package Contract

`ShaderPackage` is the format-neutral runtime shader contract. It owns a
canonical manifest plus target blobs and can be created, serialized, loaded,
capability-checked, and selected without a compiler dependency.

`truffle-shaderc` is an optional host tool. Its dependency-free path assembles
precompiled or backend-native variants. Its separately enabled pinned glslang
route compiles GLSL and GLSL ES to SPIR-V. Both paths record authoring-language
provenance, source SHA-256 values, and compiler identity. The tool never fetches
or discovers a compiler implicitly. Additional source-to-target compiler
adapters must arrive with their pinned submodules and explicit enable flags.

## Inputs And Variants

Recorded authoring routes are Slang, HLSL, GLSL, GLSL ES, WGSL, and MSL.
Accepted precompiled routes are SPIR-V, DXIL, Metal libraries, and
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
[the ShaderPackage format specification](shader-package-format.md). It uses a
fixed little-endian header, a canonical manifest, ordered target blobs, and
per-section integrity hashes. Load rejects incompatible versions, invalid enum
values, excessive counts or sizes, corrupt hashes, non-canonical ordering, and
trailing data.

## Compilation Policy

Offline binaries are the production default. Runtime compilation is optional,
capability-reported, and backend-local.

Compilation routes may use pinned Slang, DXC, glslang, SPIR-V Tools,
SPIRV-Cross, and Tint components. Glslang is linked only by the explicitly
enabled offline GLSL route and never by the runtime or dependency-free assembly
configuration. Slang is one authoring route, not Truffle's
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
  package while keeping pipeline capability separately gated.
- `truffle-shaderc` self-test and file-based assemble/inspect smoke tests run in
  CI when `TRUFFLE_BUILD_SHADERC=ON`.
- With `TRUFFLE_SHADERC_ENABLE_GLSLANG=ON`, CI compiles a representative shader
  twice to prove deterministic SPIR-V packages and requires invalid GLSL to
  return bounded compiler diagnostics.

Native execution on each additional backend remains its own evidence gate.
Recording a target blob does not by itself advance a backend maturity level.

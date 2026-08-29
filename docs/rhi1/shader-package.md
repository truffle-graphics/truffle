# RHI 1 Shader Package Contract

`ShaderPackage` is the format-neutral runtime shader contract. It owns a
canonical manifest plus target blobs and can be created, serialized, loaded,
capability-checked, and selected without a compiler dependency.

`truffle-shaderc` is an optional host tool. Its current dependency-free path
assembles precompiled or backend-native variants and records authoring-language
provenance, source SHA-256 values, and compiler identity. It never fetches or
discovers a compiler implicitly. Future source-to-target compiler adapters must
arrive with their pinned submodules and explicit enable flags.

## Inputs And Variants

Recorded authoring routes are Slang, HLSL, GLSL, GLSL ES, WGSL, and MSL.
Accepted precompiled routes are SPIR-V, DXIL, Metal libraries, and
backend-native source variants.

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
SPIRV-Cross, and Tint components. None is currently linked by the runtime or the
dependency-free assembly tool. Slang is one authoring route, not Truffle's
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
  selected package variant without compiler linkage.
- `truffle-shaderc` self-test and file-based assemble/inspect smoke tests run in
  CI when `TRUFFLE_BUILD_SHADERC=ON`.

Native Metal, Vulkan, and D3D12 execution of the same package remains a pipeline
and backend evidence gate. Recording a target blob does not by itself advance a
backend maturity level.

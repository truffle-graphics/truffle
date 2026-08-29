# RHI 1 Shader Package Direction

`ShaderPackage` is the format-neutral runtime shader contract.
`truffle-shaderc` is an optional host tool that produces packages; it is not a
runtime dependency of every Truffle consumer.

## Inputs And Variants

Accepted authoring routes are Slang, HLSL, GLSL, GLSL ES, WGSL, and MSL.
Accepted precompiled routes are SPIR-V, DXIL, Metal libraries, and
backend-native source variants.

A package may carry several target variants at once: SPIR-V, DXIL, MSL or
`metallib`, WGSL, desktop GLSL, and GLSL ES. A native variant can override a
generated variant for one target.

Every package records:

- entry points and stages;
- permutations, defines, and specialization constants;
- required RHI capabilities;
- logical binding layouts and per-target remapping;
- vertex and fragment interfaces;
- normalized reflection data and source hashes;
- compiler identities and versions;
- bounded diagnostics and provenance for every variant.

Reflection for all selected variants must normalize to the same logical
contract. Package creation fails if resource types, arrays, access, stage
visibility, push-constant ranges, specialization constants, or interfaces do
not agree.

## Compilation Policy

Offline binaries are the production default. Runtime compilation is optional,
capability-reported, and backend-local.

Compilation routes may use pinned Slang, DXC, glslang, SPIR-V Tools,
SPIRV-Cross, and Tint components. Slang is one authoring route, not Truffle's
mandatory shader language. Experimental compiler targets, including any route
whose Metal or WebGPU output has not passed native package tests, remain
explicitly gated.

## Acceptance

The shader package slice is not complete until:

- a versioned deterministic package format and schema exist;
- corrupt, incompatible, and capability-mismatched packages fail explicitly;
- target selection is deterministic and diagnostic;
- reflection equivalence has positive and negative fixtures;
- at least Metal, Vulkan, and D3D12 variants execute the same package in native
  output tests;
- packages and the optional compiler tool can be built reproducibly from the
  declared dependency lock.

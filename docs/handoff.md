# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Finish the deterministic ShaderPackage and optional package-tool PR for issue
#32, then begin immutable bindings and graphics/compute pipelines under #30.

## Latest Handoff

- Merged PRs #36, #37, and #38 into `develop`, closing the warning-clean
  baseline, RHI 1 contract/truth reset, and public foundation cutover in
  #26–#28.
- The separate companion-task router still fails before dispatch because its
  configured GitHub agent token receives `401 Bad credentials`. This is a
  repository-automation credential problem, not an engine validation failure.
- On `feat/rhi1-resources` for #29, added the complete public resource contract:
  buffer and texture dimensions/formats/usages, subresources and typed views,
  mapping/coherence, memory requirements and budgets, caller allocation hooks,
  external-memory handles, and copy/fill/clear/resolve/blit commands.
- Null now executes and validates the full resource/transfer contract, including
  mip, array, cube, 3D, block-compressed, lifetime-retention, out-of-memory, and
  accurate unsupported paths.
- Metal now allocates native buffers and selected single-layer 2D textures,
  creates compatible native texture views, maps coherent host-visible memory,
  and executes buffer, texture, and buffer-texture transfers with native
  command encoders. Unsupported shapes and operations are capability-reported.
- A host-hardware run with Metal API validation exercised exact buffer and
  texture roundtrips, padded-row buffer/texture copies, a format-reinterpreting
  view, and a GPU fill. The live reporter classified Metal as `native_smoke`.
- The macOS CI test step now sets `MTL_DEBUG_LAYER=1`; hosts without a Metal
  adapter retain the explicit unavailable/skip path rather than simulating one.
- Merged PR #39 into `develop` after package, macOS, Ubuntu, and Windows passed;
  issue #29 was closed explicitly because `develop` is not GitHub's default
  branch.
- On `feat/rhi1-shader-package` for #32, implemented ShaderPackage schema 1.0 as
  a canonical little-endian manifest/blob container with bounded decoding,
  per-section integrity hashes, capability gating, source/compiler provenance,
  logical remaps, normalized reflection, and deterministic variant selection.
- Native overrides win over precompiled and generated variants. Reflection
  disagreement across targets rejects package creation; generated Metal and
  WGSL routes remain disabled unless explicitly opted into as experimental.
- `Device::create_shader` can consume an exact package target variant without a
  compiler. Optional `truffle-shaderc` assembles, appends, and inspects
  multi-target package files while adding no runtime or configure-time download.

## Durable Decisions

- RHI 1 immediately replaces the preliminary public API. There is no public
  shim or parallel backend-facing API.
- Public objects are move-only RAII wrappers over opaque generation-checked
  handles and backend dispatch.
- Portable typed outcomes and optional backend diagnostics are separate layers.
- Resource creation and queue submission are thread-safe; command recording and
  descriptor-arena mutation are single-thread-owned.
- Retirement waits for referenced queue timeline completion.
- Presentation is a queue operation; the host owns the native window/layer and
  application lifecycle.
- Renderer descriptor caching/admission/recovery policy is removed from core
  RHI.
- Bundled pinned source is the default for optional dependencies; configure
  performs no network fetches. Platform SDKs remain explicit prerequisites.
- Backend/platform maturity and feature support are separate and evidence-based.
- The Simple RHI remains deferred.
- ShaderPackage serialization is canonical and versioned. Source-to-target
  compiler adapters remain separate optional targets and must introduce their
  pinned submodules only with an implemented route.

## Validation

Baseline PR #36:

```text
cmake --preset ci
cmake --build --preset ci
ctest --preset ci --output-on-failure  # 26/26
GitHub Build: package, macOS, Ubuntu, Windows passed
```

Issue #29 validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci
MTL_DEBUG_LAYER=1 ctest --preset ci --output-on-failure  # 27/27
MTL_DEBUG_LAYER=1 build/ci/tests/truffle_metal_tests      # host GPU access
build/ci/tests/truffle_rhi_parity_report <output.json>    # host GPU access
cmake -DTRUFFLE_BUILD_DIR=<build/ci> \
      -DTRUFFLE_REPORT_OUT=<parity-matrix.md> \
      -P cmake/GenerateParityReport.cmake
cmake --build --preset ci --target package
git diff --check
```

All 27 tests pass with warnings-as-errors, including the new exhaustive Null
resource suite, installed package consumer, and all enabled backend contracts.
The restricted test environment cannot discover the host GPU, so its generated
parity artifact conservatively records Metal as unavailable `cross_compiles`.
Running the same validation-enabled Metal binary and reporter with host hardware
access succeeds and reports Metal as available native `native_smoke`. The
package archive builds and `git diff --check` passes. `truffle_format_check`
cannot run because `clang-format` is not installed on this host.

Issue #32 validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci
MTL_DEBUG_LAYER=1 ctest --preset ci --output-on-failure  # 30/30
ctest --test-dir build/ci -R truffle_shader --output-on-failure  # 3/3
cmake --build --preset ci --target package
git diff --check
```

The package tests prove byte-identical canonical output, positive and negative
reflection equivalence, capability mismatch, corruption and trailing-data
rejection, native override precedence, experimental target gates, and
package-to-Null shader creation. The tool self-test plus file smoke assembles,
appends, and inspects a two-target package.

## Next Resume Steps

1. Open and merge the #32 PR after package, macOS, Ubuntu, and Windows Build
   jobs pass; close #32 explicitly if needed.
2. Start #30 from fresh `develop`: immutable ordinary bindings first, then
   pipeline layouts/state and command coverage.
3. Preserve deterministic Null semantics and require native backend evidence for
   every new pipeline or binding capability claimed.

## Open Risks

- The companion-task GitHub credential must be repaired outside the engine
  codebase; retries with the same credential will continue to fail.
- Metal supports only the documented resource subset; texture clear/resolve/
  blit, pipelines, synchronization, recovery, and presentation remain pending.
- Vulkan, OpenGL, and Direct3D targets are factories without native adapters;
  their package presence must not be described as backend support.
- Native SDK and toolchain versions must be pinned when each dependency-bearing
  target is introduced, rather than guessed in advance.
- The dependency-free package tool does not compile Slang/HLSL/GLSL/WGSL/MSL;
  it records their provenance beside externally produced target variants.

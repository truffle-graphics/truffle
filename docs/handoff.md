# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Finish issue #30's immutable bindings and native graphics/compute pipeline PR,
then begin explicit synchronization and presentation under issue #31.

## Latest Handoff

- PRs #36-#40 are merged into `develop`, completing the warning-clean baseline,
  RHI 1 foundation, resources, and deterministic ShaderPackage 1.0 slices.
- On `feat/rhi1-bindings-pipelines` for #30, added immutable samplers, bind-group
  layouts and groups, descriptor arenas with epoch invalidation, descriptor
  arrays, aligned dynamic offsets, pipeline layouts, push and specialization
  constants, optional bindless and pipeline-cache objects, and capability
  limits.
- Graphics pipelines now validate shader reflection and own vertex input,
  topology, rasterization, color/blend, depth/stencil, multisample, and static or
  dynamic state. Compute pipelines validate reflected required/preferred
  workgroup sizes against adapter limits.
- Render and compute encoders now retain complete direct, indexed, instanced,
  indirect, indirect-count, and dispatch-indirect arguments. Binding uses stable
  logical `(group, binding, arrayElement)` identity plus target-specific shader
  remaps.
- Null validates the complete #30 contract, including descriptor capacity and
  lifetime, layout/reflection mismatch, command completeness, cache round-trip,
  and explicit unsupported bindless/update-after-bind paths.
- Metal creates native samplers, MSL functions, function constants, render and
  compute pipeline state, and command encoders. It implements descriptor arrays,
  dynamic offsets, immutable samplers, push constants, logical remaps, MRT,
  depth/stencil state, MSAA resolves, indirect drawing, and indirect compute.
- Metal API validation exact-output proofs cover ShaderPackage-to-triangle,
  textured descriptor arrays with a dynamic uniform offset and specialization/
  push constants, depth-tested MRT with 4x MSAA, indexed/instanced/indirect
  drawing, and compute-to-render storage flow.
- Metal truthfully reports bindless/update-after-bind, indirect-count,
  serialized pipeline caches, tessellation, and presentation as unsupported.
  Those limitations do not fall back to Null behavior.
- Renderer pipeline preparation now matches its placeholder pipeline to the
  current pass format, depth format, and sample count, preserving existing
  renderer tests under the stricter attachment compatibility rules.

## Durable Decisions

- RHI 1 immediately replaces the preliminary public API. There is no public
  shim or parallel backend-facing API.
- Public objects are move-only RAII wrappers over opaque generation-checked
  handles and backend dispatch.
- Portable typed outcomes and optional backend diagnostics are separate layers.
- Resource creation and queue submission are thread-safe; command recording and
  descriptor-arena mutation are single-thread-owned.
- Ordinary bind groups, layouts, and recorded resources are immutable. Arena
  reset advances an epoch and invalidates groups allocated from older epochs.
- Shader bindings retain logical identity across targets. Native remaps never
  change reflection or application layout identity.
- Pipeline creation deterministically rejects layout/reflection, push-constant,
  specialization, attachment, and workgroup mismatches.
- Optional bindless, update-after-bind, pipeline cache, tessellation, and
  indirect-count behavior is capability-reported and must fail explicitly when
  unavailable.
- Retirement waits for referenced queue timeline completion.
- Presentation is a queue operation; the host owns the native window/layer and
  application lifecycle.
- Renderer descriptor caching/admission/recovery policy remains above core RHI.
- Bundled pinned source is the default for optional dependencies; configure
  performs no network fetches. Platform SDKs remain explicit prerequisites.
- Backend/platform maturity and feature support are separate and evidence-based.
- ShaderPackage serialization is canonical and versioned. Runtime compiler
  adapters remain optional and backend-local.
- The Simple RHI remains deferred.

## Validation

Issue #30 validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci -j 6
MTL_DEBUG_LAYER=1 ctest --preset ci --output-on-failure  # 31/31
MTL_DEBUG_LAYER=1 build/ci/tests/truffle_metal_tests
cmake --build --preset ci --target package
git diff --check
```

The dedicated Null binding/pipeline suite exercises the complete portable
contract and negative paths. The validation-enabled Metal executable verifies
exact native pixels and buffer values for all five acceptance groups listed
above. The full suite and package archive pass with warnings-as-errors.
`truffle_format_check` remains unavailable because `clang-format` is not
installed on this host.

## Next Resume Steps

1. Open the #30 PR, monitor package/macOS/Ubuntu/Windows jobs, repair any
   compiler-specific diagnostics, merge into `develop`, and close #30 if GitHub
   does not close it automatically.
2. Start #31 from fresh `develop` and preserve command ordering while adding
   barriers, wait/signal values, fences, queries, acquisition, and presentation.
3. Keep Metal at `native_smoke` until synchronization, recovery, presentation,
   and the remaining conformance evidence are repeatable.

## Open Risks

- Transfer commands and graphics/compute native commands currently occupy
  separate internal streams. Issue #31 must unify ordering before mixed
  copy/render/compute command lists can claim explicit synchronization
  correctness.
- Metal intentionally lacks serialized pipeline caches, indirect-count,
  bindless/update-after-bind, and tessellation in this slice.
- Vulkan, OpenGL, and Direct3D targets are factories without native adapters;
  their package presence must not be described as backend support.
- The companion-task GitHub credential still fails before dispatch with `401
  Bad credentials`; this is external repository automation, not engine
  validation.

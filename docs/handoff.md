# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

RHI 1 Phase 2: resources, memory, views, and transfers under issue #29, after
the completed public foundation cutover in #28.

## Latest Handoff

- Merged PRs #36 and #37 into `develop`, closing the warning-clean baseline and
  RHI 1 contract/truth reset in #26 and #27.
- The separate companion-task router still fails before dispatch because its
  configured GitHub agent token receives `401 Bad credentials`. This is a
  repository-automation credential problem, not an engine validation failure.
- On `feat/rhi1-foundation` for #28, replaced the exported virtual RHI with
  focused RHI 1 headers and move-only RAII wrappers over private,
  generation-checked object handles and dispatch tables.
- Migrated render, scene, examples, diagnostics, package consumers, and tests to
  the sole new API. Deleted the preliminary descriptor admission/recovery
  policy engine rather than carrying it forward.
- Null now proves foundation object lifetime, stale-handle rejection,
  cross-runtime rejection, command retention/state, resource-creation
  concurrency, command-pool thread ownership, synchronization, swapchain, and
  presentation flow.
- Metal uses the same foundation for native device discovery and empty native
  command-buffer submission. It reports only graphics submission capability;
  resources, synchronization, compute, and presentation remain explicitly
  unsupported.
- Removed simulated Vulkan, OpenGL, and Direct3D adapters. Their named factories
  now return `unsupported` until native implementations land.
- Added the system thread link requirement to the build and installed CMake
  package contract.

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

## Validation

Baseline PR #36:

```text
cmake --preset ci
cmake --build --preset ci
ctest --preset ci --output-on-failure  # 26/26
GitHub Build: package, macOS, Ubuntu, Windows passed
```

Issue #28 validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci
ctest --preset ci --output-on-failure  # 26/26
cmake -DTRUFFLE_BUILD_DIR=<build/ci> \
      -DTRUFFLE_REPORT_OUT=<parity-matrix.md> \
      -P cmake/GenerateParityReport.cmake
python3 -m json.tool build/ci/parity-matrix.json
python3 -m json.tool build/ci/rhi-parity-report.json
git diff --check
```

All 26 tests pass with warnings-as-errors, including the installed package
consumer and all enabled backend contracts. Generated parity JSON reports Null
as validation-only, Metal as `cross_compiles` but unavailable on this host, and
Vulkan/OpenGL/Direct3D as unavailable `source_only` targets. `git diff --check`
passes. `truffle_format_check` cannot run because `clang-format` is not installed
on this host.

## Next Resume Steps

1. Merge #28 after the package, macOS, Ubuntu, and Windows Build jobs pass.
2. Start #29 from fresh `develop`.
3. Expand resources and transfers vertically: typed views/subresources, memory
   domains and mapping coherence, caller allocation, copy/fill/clear/resolve/
   blit, upload/readback, and deterministic data evidence in Null and Metal.

## Open Risks

- The companion-task GitHub credential must be repaired outside the engine
  codebase; retries with the same credential will continue to fail.
- Metal resource and presentation capability remains intentionally unavailable;
  empty native submission is not enough to advance its maturity.
- Vulkan, OpenGL, and Direct3D targets are factories without native adapters;
  their package presence must not be described as backend support.
- Native SDK and toolchain versions must be pinned when each dependency-bearing
  target is introduced, rather than guessed in advance.

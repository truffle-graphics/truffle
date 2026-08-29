# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

RHI 1 Phase 1: breaking public object and dispatch cutover under issue #28.

## Latest Handoff

- Created milestone `Truffle RHI 1`, parent issue #25, and issue-scoped work
  items #26-#35.
- Merged PR #36 into `develop` and closed #26. The package, macOS, Ubuntu, and
  Windows warning-clean Build jobs passed.
- The separate companion-task router still fails before dispatch because its
  configured GitHub agent token receives `401 Bad credentials`. This is a
  repository-automation credential problem, not an engine validation failure.
- On `docs/rhi1-contract` for #27, added ADR 0011 plus RHI 1 ownership,
  threading, lifetime, outcome, dependency, shader-package, and evidence-based
  maturity specifications.
- Reclassified the current backend evidence: Null is validation-only; Metal is
  `cross_compiles` on macOS pending accepted validation-enabled smoke evidence;
  Vulkan, Direct3D, and OpenGL are contract simulators and only `source_only`
  for their future native implementations.
- Replaced the old completion ladder with the issue-aligned RHI 1 roadmap.
- PR #37 passes the package, macOS, Ubuntu, and Windows Build jobs and completes
  issue #27's Phase 0 truth reset.

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

Issue #27 validation on macOS Apple Silicon:

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

All passed. New relative documentation link targets were inspected locally;
the repository currently has no automated Markdown link-check command.

PR #37 GitHub Build also passed package, macOS, Ubuntu, and Windows jobs.

## Next Resume Steps

1. Start #28 from fresh `develop`.
2. Implement the object/dispatch foundation and migrate in-tree consumers in a
   single breaking public cutover.
3. Delete the preliminary descriptor policy engine instead of adapting it to
   the new dispatch layer.

## Open Risks

- The companion-task GitHub credential must be repaired outside the engine
  codebase; retries with the same credential will continue to fail.
- The current public header and simulator backends remain until #28/#33; their
  presence must not be described as RHI 1 completion.
- Native SDK and toolchain versions must be pinned when each dependency-bearing
  target is introduced, rather than guessed in advance.

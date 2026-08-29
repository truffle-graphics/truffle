# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Current Focus

Finish issue #31's explicit synchronization and native Metal presentation PR,
then begin native backend/platform breadth under issue #33.

## Latest Handoff

- PRs #36-#41 are merged into `develop`, completing the warning-clean baseline,
  RHI 1 foundation, resources, ShaderPackage 1.0, immutable bindings, and native
  graphics/compute pipeline slices.
- On `feat/rhi1-sync-presentation` for #31, command lists now retain one ordered
  transfer/barrier/render/compute stream. Mixed render-to-copy validation proves
  that submission no longer moves every transfer ahead of encoder commands.
- The public synchronization contract now has pipeline-stage/access flags,
  texture layouts, buffer/texture subresource barriers, aliasing barriers,
  queue-ownership transfer, timeline semaphore waits/signals, optional timeline
  fence signals, and bounded wait timeouts.
- Null tracks subresource layouts and queue ownership, preserves executable
  command lists after timeout/failure, and validates multi-queue timeline,
  aliasing, stale-layout, acquisition, presentation-wait, and resize paths.
- Metal maps semaphores to `MTLSharedEvent`, preserves barriers at encoder
  boundaries, accepts host-owned `CAMetalLayer` surfaces, acquires drawable
  textures, and presents them only through a graphics queue.
- Metal reports drawable drift as `suboptimal`, zero extent as `out_of_date`,
  drawable exhaustion as `timeout`, detached layers as `surface_lost`, and
  device-removal command errors as `device_lost`. A resize recovers the tested
  out-of-date path; device loss marks the public device lost.
- A private, non-installed Metal fault hook proves `device_lost` propagation,
  rejection of further work on the lost device, and recovery through a fresh
  device without claiming physical GPU removal.
- PR #42's first macOS runner showed that assigning a zero `drawableSize` is not
  deterministic across `CAMetalLayer` implementations. The native test now
  injects only the otherwise-unreliable `out_of_date` acquisition result through
  the same private hook, then proves resize recovery through the real layer.
- Renderer swapchain flow now records present/color transitions, waits on the
  acquisition point, signals rendering completion, and passes that wait to
  queue presentation.
- The validation-enabled macOS suite is 32/32. Dedicated Metal tests prove
  ordered render-to-copy output, shared-event/fence completion, native
  acquire/render/present, suboptimal acquisition, resize, out-of-date recovery,
  surface-loss recreation, and injected device-loss recovery.

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

Issue #31 validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci -j 6
MTL_DEBUG_LAYER=1 ctest --preset ci --output-on-failure  # 32/32
MTL_DEBUG_LAYER=1 build/ci/tests/truffle_metal_tests
MTL_DEBUG_LAYER=1 build/ci/tests/truffle_metal_presentation_tests
cmake --build --preset ci --target package
git diff --check

cmake --preset ci -B /tmp/truffle-rhi31-gcc \
      -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15 \
      -DTRUFFLE_BUILD_BACKEND_METAL=OFF \
      -DTRUFFLE_BUILD_EXAMPLES=OFF
cmake --build /tmp/truffle-rhi31-gcc -j 6
ctest --test-dir /tmp/truffle-rhi31-gcc --output-on-failure  # 26/26
```

The GCC suite passes 26/26 and the package archive builds. The Null suite
exercises the portable synchronization and presentation
contract and negative paths. The validation-enabled Metal executables verify
exact native ordering and layer presentation/status behavior. The full suite
and package archive pass with warnings-as-errors. `truffle_format_check` remains
unavailable because `clang-format` is not installed on this host.

## Next Resume Steps

1. Open the #31 PR, monitor package/macOS/Ubuntu/Windows jobs, repair any
   compiler-specific diagnostics, merge into `develop`, and close #31 if GitHub
   does not close it automatically.
2. Start #33 from fresh `develop`; keep Metal at `native_smoke` until host-window,
   physical device-loss, broader platform, and remaining conformance evidence
   are repeatable.

## Open Risks

- Submission is currently synchronous. It gives correct completed fence values
  but does not yet provide asynchronous queue throughput or full deferred
  destruction.
- Metal's barrier mapping relies on encoder boundaries and default resource
  hazard tracking; aliased heap allocations are not implemented yet.
- Device-removal mapping and private fault injection are tested, but physical
  device-removal evidence is not available in the macOS CI harness.
- Metal intentionally lacks serialized pipeline caches, indirect-count,
  bindless/update-after-bind, and tessellation in this slice.
- Vulkan, OpenGL, and Direct3D targets are factories without native adapters;
  their package presence must not be described as backend support.
- The companion-task GitHub credential still fails before dispatch with `401
  Bad credentials`; this is external repository automation, not engine
  validation.

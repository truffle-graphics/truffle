# Roadmap

## Program Rule

This roadmap tracks the RHI 1 replacement through issue-scoped pull requests.
A shared contract, simulator, or compile-only result is not a native-backend
completion claim. Backend-platform maturity follows
`docs/rhi1/support-matrix.md` and can advance independently.

The earlier Phase 1-12 labels described useful preliminary contracts and tests,
but their “Complete” wording did not prove native GPU execution or platform
support. They are historical inputs, not the current delivery gates.

## Phase 0: Restore Baseline And Project Truth — Complete

- [x] #26 restore warning-clean package, macOS, Ubuntu, and Windows CI.
- [x] #27 accept the RHI 1 ADR, public ownership/threading/lifetime rules,
  dependency policy, shader-package direction, target matrix, and objective
  maturity gates.

Exit: repository documentation makes the native/simulator distinction
consistently and no phase is promoted without its required evidence.

## Phase 1: Public Object And Dispatch Foundation — Complete

Tracked by #28.

- [x] Replace the preliminary monolithic interface with focused headers and an
  umbrella include.
- [x] Add portable `Status`/`Result<T>` outcomes and optional backend diagnostics.
- [x] Implement `Instance`, adapter discovery, feature-negotiated `Device`, queues,
  opaque generation-checked handles, dispatch, and strict Null semantics.
- [x] Migrate all in-tree consumers and delete the preliminary public API and
  descriptor policy engine in the same breaking cutover.

Exit: RHI 1 is the sole exported graphics API; Null proves foundation lifetime,
state, and threading behavior; Metal uses the same foundation over native
device/command submission; named unimplemented backends report no adapters.

## Phase 2: Resources, Memory, Views, And Transfers — Complete

Tracked by #29.

- [x] Implement complete buffer/texture dimensions, formats, subresources, views,
  memory domains, mapping/flush/invalidate, external sharing where supported,
  memory budgets, caller allocators, upload/readback, copy/fill/clear/resolve,
  and blit commands.
- [x] Require deterministic native readback or output evidence per backend slice.

Exit: Null validates the full contract, including accurate unsupported paths and
deferred retirement. Metal passes validation-enabled native buffer, texture, and
buffer-texture round trips; unsupported shapes and operations are capability
reported. Other named backends still expose no adapter.

## Phase 3: Bindings, Shaders, And Pipelines — Pending

Tracked by #30 and #32.

- Implement immutable ordinary bind groups, descriptor arenas, arrays, dynamic
  offsets, specialization constants, optional push constants and bindless
  tables, pipeline layouts/caches, full graphics state, and compute workgroup
  requirements.
- Introduce `ShaderPackage` and optional `truffle-shaderc`; validate reflection
  equivalence across selected target variants.

## Phase 4: Explicit Synchronization And Presentation — Pending

Tracked by #31.

- Implement stage/access/layout barriers, subresource and aliasing barriers,
  queue ownership transfer, semaphore values, fences, and query pools.
- Make submission accept explicit command-list and wait/signal arrays.
- Make acquisition return image/index/status/synchronization and move
  presentation to `Queue::present()`.

## Phase 5: Native Backend And Platform Matrix — Pending

Tracked by #33.

- Metal across the declared Apple targets.
- Vulkan on Windows, Linux, Android, and Apple through MoltenVK.
- D3D12 on Windows with WARP validation.
- WebGPU on wasm32 through Emdawnwebgpu.
- OpenGL 4.5, OpenGL ES 3.1/EGL, and WebGL2 compatibility implementations.

Every backend-platform pair advances from `source_only` through `supported`
only when it meets the published evidence gate. Unsupported features fail
explicitly and remain visible in capabilities.

## Phase 6: Advanced Capability Tracks — Pending

Tracked by #34.

- Acceleration structures/ray queries and full ray pipelines where native.
- Task/mesh shaders.
- Variable-rate rendering and Metal rasterization-rate maps.
- Sparse resources and residency.
- Multiview and native query families.

Each subsystem has a distinct capability contract and native conformance suite.
Standard/KHR/ARB GL-family features may participate; vendor-only extensions are
deferred.

## Phase 7: Harden, Package, And Promote — Pending

Tracked by #35.

- Optimize allocation, command recycling, descriptor use, deferred deletion,
  and pipeline caches only after native correctness.
- Run validation layers and platform-specific recovery suites.
- Package enabled backends without leaking disabled dependencies.
- Promote backend-platform pairs individually and publish their evidence.

RHI 1 is complete when its contract and governance are stable and every target
has a truthful published maturity state. Not every backend must be `supported`
at the same time.

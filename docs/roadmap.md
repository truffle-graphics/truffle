# Roadmap

## Program Rule

This roadmap tracks the RHI 1 replacement through issue-scoped pull requests.
A shared contract, simulator, or compile-only result is not a native-backend
completion claim. Backend-platform maturity follows
`docs/rhi1/support-matrix.md` and can advance independently.

The earlier Phase 1-12 labels described useful preliminary contracts and tests,
but their “Complete” wording did not prove native GPU execution or platform
support. They are historical inputs, not the current delivery gates.

The [Truffle delivery project](https://github.com/users/loosewired/projects/5)
is the cross-machine execution view. Issue #25 is the program parent; phase
issues are epics, and their structurally linked sub-issues contain the
executable scope, dependencies, acceptance criteria, and evidence requirements.
See `docs/planning.md` for the durable tracking contract.

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

## Phase 3: Bindings, Shaders, And Pipelines — Complete

Tracked by #30 and #32.

- [x] Implement immutable ordinary bind groups, descriptor arenas, arrays, dynamic
  offsets, specialization constants, optional push constants and bindless
  tables, pipeline layouts/caches, full graphics state, and compute workgroup
  requirements.
- [x] Introduce deterministic `ShaderPackage` 1.0 and optional dependency-free
  `truffle-shaderc`; validate reflection equivalence across selected target
  variants and gate experimental Metal/WGSL generation.

Exit: Null validates logical identity, lifetime, reflection, state, direct and
indirect command forms, cache behavior, and accurate optional-feature failures.
Metal passes validation-enabled exact-output graphics and compute proofs using
the same ShaderPackage and logical-remap contract; its unimplemented optional
features remain capability-reported.

## Phase 4: Explicit Synchronization And Presentation — Complete

Tracked by #31.

- [x] Implement stage/access/layout barriers, subresource and aliasing barriers,
  queue ownership transfer, semaphore values, fences, and query pools.
- [x] Make submission accept explicit command-list and wait/signal arrays.
- [x] Make acquisition return image/index/status/synchronization and move
  presentation to `Queue::present()`.

Exit: one ordered command stream preserves copy/render/compute ordering. Null
validates timeline waits, timeout retry, subresource layouts, aliasing, and
multi-queue ownership. Metal maps semaphores to shared events and passes
validation-enabled native `CAMetalLayer` acquire/render/present, resize,
suboptimal, out-of-date, and surface-loss proofs.

## Phase 5: Native Backend And Platform Matrix — In Progress

Tracked by #33.

- [x] Publish backend/platform maturity and per-evidence dimensions through the
  public API and strict JSON doctor; keep Null validation-only.
- [x] Pin Vulkan-Headers and volk privately and add Linux native
  instance/device/queue/command-buffer smoke.
- [x] Add Windows SDK D3D12 WARP device/command-list smoke.
- [x] Add Linux surfaceless EGL OpenGL/OpenGL ES deterministic clear/readback
  smoke.
- [x] Reconcile merged native resource evidence and public matrix claims in
  #48.
- [ ] Complete the compiler prerequisites in #73.
- [x] Complete D3D12 baseline resources and ShaderPackage-backed graphics and
  compute pipelines through #49-#50.
- [x] Complete D3D12 synchronization, barrier, and query paths through #51.
- [x] Complete D3D12 DXGI presentation through #52.
- [ ] Complete Vulkan baseline capability and Linux, Windows, Android, and
  MoltenVK platform lanes through #53-#59.
  - [x] Complete Linux Vulkan ShaderPackage bindings and graphics/compute
    pipelines with deterministic native output through #53.
  - [x] Complete Vulkan queue discovery, synchronization, barriers, and query
    pools with validation-enabled Linux evidence through #54.
  - [x] Complete host-owned Linux XCB surfaces, native acquire/present, exact
    swapchain readback, and resize recreation through #57.
- [ ] Complete OpenGL/OpenGL ES resources, pipelines, synchronization, and
  desktop/Android presentation through #60-#64.
  - [x] Complete shared GL-family resources and transfers with separate desktop
    GL/GLES capability and KHR_debug evidence through #60.
- [ ] Complete WebGPU and WebGL2 runtime, rendering, browser presentation, and
  recovery through #65-#68 and #70.
- [ ] Complete Metal macOS conformance plus Apple cross-build and runtime
  evidence through #69 and #71-#72.
- [ ] Close the phase only through the evidence audit in #74.

Every backend-platform pair advances from `source_only` through `supported`
only when it meets the published evidence gate. Unsupported features fail
explicitly and remain visible in capabilities.

## Phase 6: Advanced Capability Tracks — Pending

Tracked by #34.

- [ ] Acceleration-structure, ray-query, and ray-pipeline contracts and native
  implementations: #75-#78.
- [ ] Task/mesh shader contracts and Metal, Vulkan, and D3D12 implementations:
  #79-#82.
- [ ] Variable-rate rendering contracts and native implementations: #83-#86.
- [ ] Sparse resource/residency contracts and native implementations: #87-#90.
- [ ] Multiview contracts and native/standardized implementations: #91-#94 and
  #98.
- [ ] Query-family contracts and native/standardized implementations: #95-#100.
- [ ] Close the phase only through the conformance audit in #101.

Each subsystem has a distinct capability contract and native conformance suite.
Standard/KHR/ARB GL-family features may participate; vendor-only extensions are
deferred.

## Phase 7: Harden, Package, And Promote — Pending

Tracked by #35.

- [ ] Implement asynchronous submission and deferred retirement through
  #102-#105 and #107.
- [ ] Optimize backend allocation and transient-resource reuse through #106,
  #108-#109, and #112.
- [ ] Harden descriptor, pipeline, and command-object reuse through #110-#111
  and #113.
- [ ] Complete package isolation, recovery, conformance, validation,
  performance, provenance, and evidence publication through #114-#120.
- [ ] Make independent backend/platform maturity decisions through #121-#126.
- [ ] Close the program through the final acceptance and contract-freeze work
  in #127-#128.

RHI 1 is complete when its contract and governance are stable and every target
has a truthful published maturity state. Not every backend must be `supported`
at the same time.

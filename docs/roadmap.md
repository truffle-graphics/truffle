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

## Current Priorities

The practical Apple Silicon backend baseline is established: Metal (#69),
Vulkan via MoltenVK (#56), and OpenGL 4.1 host-view presentation and
synchronization (#61/#63 macOS parts). Other-platform verification remains
non-blocking.

The next work is parallel Shader and Renderer development:

1. **Shader library (#178):** shared source composition/configuration,
   optional offline/runtime compilation, reflection, dependencies, caching,
   and rebuilding, initially using pinned GLSL/GLSL ES to SPIR-V.
2. **Shader tools (#180):** clients of that library, with Truffle Shader
   Package (TSP) naming and `.tsp` artifacts. This follows the library API.
3. **Renderer (#179):** prepared-TSP GPU shader/pipeline ownership and real
   frame-graph/batch execution. This runs independently of source compilation
   and does not require EnCom or compiler dependencies.
4. **Runtime integration (#181):** compose the completed tracks for on-demand
   compilation and safe shader rebuild adoption with native output evidence.

The common compiled-result boundary is the existing RHI ShaderPackage;
optional runtime compilation additionally uses the Shader request/result
contract. Neither track waits for every compiler route in #73. Remaining
languages/targets keep their tracking there. Real Renderer use of the RHI is
expected to expose gaps, and fixing them is active work, not deferred work.

A practical baseline means resources, shaders, drawing and compute where
supported, synchronization, presentation, and usable error handling. It does
not mean every advanced feature on every platform.

## Verification Policy

Verification is local. Hosted GitHub Actions stay disabled to avoid plan and
minute limits; a spare local PC may later run checks directly or as a
self-hosted runner. Future runner setup is tracked in #165, not immediate work.

For cross-platform backends, working on one platform is a sufficient baseline
to keep developing. Other platforms are not blockers: they go on the pending
verification list below and are checked when someone works on that machine.
The support matrix only claims what has actually been verified.

macOS support targets Apple Silicon only (#171). Intel Macs are not an intended
target: code that happens to work there is fine, but no Intel verification,
lanes, or Intel-specific work is planned.

### Pending Verification

- Vulkan on Windows (#58).
- Desktop OpenGL on Windows (WGL, #61), and on Linux beyond its current EGL
  evidence, including the refactored EGL context path and window
  presentation (#61).
- Any baseline backend feature first verified on only one platform.

## Deferred Backlog

Accepted work scheduled after the current priorities. Every item keeps its
open issue, so it is not forgotten. Revisit once the baseline backends are in
use, guided by what that use revealed about the RHI.

- **Other backends and platforms:** D3D12 beyond its current baseline, WebGPU
  and WebGL2 (#65-#68, #70), Vulkan Android (#59), OpenGL ES Android (#64),
  Metal on iOS/iPadOS/tvOS/visionOS (#71-#72), and MoltenVK packaging plus
  iOS-family Vulkan lanes (#172).
- **Advanced GPU features (Phase 6):** ray tracing, mesh shaders,
  variable-rate shading, sparse resources, multiview, and extended queries
  (#75-#101). These need hardware that supports them.
- **Hardening and optimization (Phase 7):** asynchronous submission,
  allocation and memory optimization, object reuse, conformance harness,
  benchmarks, provenance, and backend promotion (#102-#128).

Correctness problems found in the baseline backends (lifetime,
synchronization, presentation) are fixed as active work, not deferred.

The phase sections below remain the full record of what is done and what is
outstanding.

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
- [ ] Complete Vulkan baseline capability and Linux, Windows, and MoltenVK
  platform lanes through #53-#58; Android (#59) is deferred.
  - [x] Complete Linux Vulkan ShaderPackage bindings and graphics/compute
    pipelines with deterministic native output through #53.
  - [x] Complete Vulkan queue discovery, synchronization, barriers, and query
    pools with validation-enabled Linux evidence through #54.
  - [x] Complete host-owned Linux XCB surfaces, native acquire/present, exact
    swapchain readback, and resize recreation through #57.
  - [x] Complete the macOS MoltenVK lane (suite, `CAMetalLayer` presentation,
    validation-clean `native_smoke`) through #56; packaging and iOS-family
    lanes are deferred to #172.
- [ ] Complete OpenGL/OpenGL ES resources, pipelines, synchronization, and
  desktop presentation through #60-#63; Android GLES (#64) is deferred.
  - [x] Complete shared GL-family resources and transfers with separate desktop
    GL/GLES capability and KHR_debug evidence through #60.
  - [x] Complete reflected native GLSL bindings and expanded graphics, with
    desktop GL compute/indirect/MSAA and explicit GLES 3.0 gates through #62;
    Build `40230852839` proves exact native output without changing maturity.
- [ ] Deferred: WebGPU and WebGL2 runtime, rendering, browser presentation,
  and recovery through #65-#68 and #70.
- [x] Complete Metal macOS advertised-capability conformance through #69
  (host window, readback for every advertised capability, documented
  fault-injection-only device loss); Apple cross-build and runtime evidence
  (#71-#72) is deferred.
- [ ] Close the phase only through the evidence audit in #74, scoped to the
  current priorities with deferred rows recorded truthfully.

Every backend-platform pair advances from `source_only` through `supported`
only when it meets the published evidence gate. Unsupported features fail
explicitly and remain visible in capabilities.

## Phase 6: Advanced Capability Tracks — Deferred

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

## Phase 7: Harden, Package, And Promote — Deferred

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

## Further Module Work — Not Yet Scheduled

These directions are recorded in `docs/architecture.md` and have no issues or
sequence commitment yet beyond the initial Shader/Renderer work above:

- Renderer growth beyond its initial native slice, plus expanded optional
  EnCom–Renderer integration.
- Engine with host-owned loop support; profiles and runners undecided.
- GUI above Renderer, independent of EnCom.
- Assets module; name and design undecided.
- Deferred conveniences: Simple RHI and an optional windowing wrapper.
- Expanded diagnostics/profiling and further developer tools.

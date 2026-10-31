# Truffle — Project Context

## Intent

Truffle is a modular graphics framework, composed of independently usable
libraries, built CMake-first. It provides embeddable graphics contracts and
independently linkable modules so consumers
can choose their own application host, windowing policy, simulation model, and
asset workflow.

## Non-Goals

- Truffle does not own native windowing, input policy, application lifetime, or
  a consumer's simulation model.
- Low-level runtime modules do not depend on optional diagnostics, tooling, or
  EnCom modules.
- The library does not force all consumers through one scene, asset, or render
  ingestion path.
- The future Simple RHI is not part of the RHI 1 replacement.

## Current State

The repository is replacing its preliminary RHI with **Truffle RHI 1** under
milestone `Truffle RHI 1` and tracking issue #25. The breaking public object and
dispatch cutover plus the resource, ShaderPackage, binding, and graphics/compute
pipeline, explicit synchronization, and presentation slices are complete.

Current evidence is narrower than the old roadmap claimed:

- Null implements the RHI 1 foundation as a strict validation backend, not a
  GPU backend.
- Metal discovers a native device and passes deterministic resource readback plus
  triangle, textured binding, depth/MRT/MSAA, indexed/instanced/indirect, and
  compute-to-render proofs with Metal API validation enabled. Native shared
  events and `CAMetalLayer` tests also prove ordered submission, timeline
  synchronization, acquisition, presentation, resize, and recoverable status
  paths. Host-window presentation/resize and advertised-capability readback
  are also established (#69). Its macOS maturity remains `native_smoke`;
  broader Apple-platform execution and full cross-backend conformance remain.
  Physical removal is not an Apple Silicon requirement; device-loss evidence
  is explicitly private fault injection.
- The #33 matrix work gives Linux Vulkan a pinned private headers/volk loader
  path, native buffers, host-visible linear images, capability-checked texture
  shapes/views,
  padded-row and subresource transfers, clear, resolve, and blit commands;
  host-owned XCB surface/swapchain presentation with exact readback and resize
  recreation;
  Windows D3D12 owns native upload/readback/default-heap buffers,
  device-local textures/views, buffer/texture transfers, ShaderPackage-backed
  root signatures, graphics/compute pipelines, explicit barriers, native
  timeline fences, timestamp/occlusion queries, and host-owned Win32/DXGI
  presentation on WARP; and Linux
  OpenGL/OpenGL ES own native EGL buffers, samplers, uncompressed
  color/depth/stencil textures, mip/array/3D/cube transfers and clears after
  surfaceless initialization; desktop GL additionally owns core texture views
  and multisample resolve. Both profiles consume reflected native-source
  ShaderPackage variants with logical remaps, sampled arrays, dynamic buffer
  offsets, immutable samplers, push blocks and scalar specialization
  equivalents; they own vertex/index input, instancing, depth/stencil, MRT and
  dynamic graphics state. Desktop GL additionally implements indirect drawing
  and compute-to-render; GLES 3.0 gates those paths and permits only identity
  texture views. Native-smoke maturity is unchanged. These adapters report only the capabilities
  they implement. Remaining backend/platform WSI and presentation paths, and
  unadvertised resource gaps stay unsupported rather than simulated.
- Truffle Shader Package (TSP) 1.0 is a deterministic compiler-free runtime
  contract (`truffle::rhi::ShaderPackage`, file extension `.tsp`) with a
  canonical manifest/blob container, cross-target reflection validation,
  capability gates, and native-override selection. Optional
  `truffle-shaderc` assembles precompiled or native variants without a compiler
  toolchain, or explicitly enables pinned glslang for GLSL/GLSL ES to SPIR-V.
  Optional SPIRV-Tools validation and SPIRV-Cross add bounded portable macOS
  MSL 2.1, GLSL 410 core and GLSL ES 300 generation (#187). Generated MSL remains
  experimental; exact generated resource-backed graphics agree on Vulkan/
  MoltenVK, Metal and macOS GL 4.1. GLES has compiler validation only.
  Independent optional pinned DXC now compiles HLSL 2021/SM 6.0 to validated
  SPIR-V/DXIL and reuses portable native generation (#189). Emitted-DXIL
  reflection is native DXC/PSV metadata, not a second SPIR-V compilation.
  HLSL graphics agrees on Vulkan/MoltenVK, Metal and GL 4.1; compute agrees on
  Vulkan/Metal. Windows/D3D12 native execution remains separate evidence.
  The Shader library (#178/#180) owns compilation; Renderer consumes its
  in-memory results directly through `renderer::SourceProgram` (#181).
- The optional native Slang adapter (#190) uses pinned source Slang 2026.12.1,
  native imports and component linking, fresh transitive content snapshots,
  canonical SPIR-V reflection and independent validation. It emits only the
  requested vertex/fragment/compute entry/configuration to Vulkan 1.1 /
  SPIR-V 1.3 without glslang. See `docs/shader-slang.md`; no Slang portable-native
  output or backend maturity promotion is implied.
- WebGPU and WebGL2 now have explicit source-only targets; WebGL2 contains an
  Emscripten browser-context path but has no browser execution evidence yet.
- The public support table and `truffle-rhi-doctor` expose matrix maturity and
  live initialization independently. Null is always validation-only and never
  a GPU backend.
- The package, macOS, Ubuntu, and Windows warning-clean CI baseline was restored
  by PR #36.

## Architecture And Boundaries

`truffle_base` supplies shared primitives. `truffle_rhi` defines the one
canonical backend-neutral graphics contract; `truffle_backend_*` modules own
backend implementation. Renderer, assets, EnCom, EnCom–Renderer integration, and diagnostics
remain independently linkable. Consumers can work directly with RHI, with
Renderer batches, or with the optional EnCom–Renderer integration.

RHI 1 uses move-only RAII objects over opaque generation-checked handles and a
backend dispatch table. It separates portable typed results from optional
backend diagnostics, owns explicit resource/synchronization/presentation
mechanisms, and leaves renderer descriptor caching and recovery policy above
RHI.

The host owns native windows/surfaces, event loops, input, resize policy, and
application lifecycle. Diagnostics remain opt-in and pull-based. Asset
declarations and asset-to-render planning remain metadata layers until a
consumer selects concrete loading, upload, and backend behavior.

## Constraints And Durable Decisions

- Shared status/result, handle, configuration, and version contracts belong to
  Truffle Base: `truffle_base`, `Truffle::Base`, `truffle::base`, and
  `truffle/base/` headers (#159).
- Module taxonomy and names follow #161: Truffle is a "modular graphics
  framework, composed of independently usable libraries". Renderer
  (`truffle_renderer`), EnCom (`truffle_encom`, ENtity COMponent) and the
  optional EnCom–Renderer integration (`truffle_encom_renderer`) are the
  current module names. GUI depends on Renderer but not EnCom (design in
  ADR 0013, #217; optional GUI Dock above it); Engine
  supports a host-owned loop with profiles/runners undecided; Assets is
  deferred with name and design undecided; Simple RHI and a windowing
  convenience are deferred; shaderc and rhi-doctor are tools, not modules.
  Truffle has no planned dependency on any application framework.
- macOS support targets Apple Silicon only (#171). Intel Macs are not an
  intended target; code that happens to work there is fine, but no Intel
  verification or Intel-specific work is planned.
- Truffle has no consumers yet, so it keeps no backward-compatibility,
  deprecation, or migration scaffolding: no aliases, forwarding headers,
  shims, migration tables, or API compatibility helpers (#163). Revisit at the
  first release. TSP is likewise an evolving unreleased draft: approved
  contract/codec/fixture changes retain current `1.0` identifiers. Stable
  format/versioning guarantees begin with release (#186 comment 6527097946).
- Shader mechanisms belong to an independently usable library: source
  composition/includes/imports, compile-time configuration, compilation,
  reflection, dependency tracking, caching, and rebuilding. Offline tools
  and optional runtime compilation share that library. Renderer depends on
  Shader directly (no adapter module) and owns GPU shader/pipeline
  resources, binding compatibility, and safe replacement; RHI/backends do
  not depend on the Shader library. Compiler toolchains are optional Shader
  build parts, so prepared TSP consumers remain compiler-free. Use existing shader languages, compile requested
  combinations rather than every possible permutation, and keep source
  rebuilding separate from publication of validated GPU programs
  (#73, #178-#181; see `docs/architecture.md`).
- Development is local-first under the decision in #131. Git Flow retains
  `master`, `develop`, feature/fix, release, and hotfix branches with normal
  merge commits. Hosted builds/releases are explicit manual opt-ins, not
  per-PR/push gates; automatic cloud companions are retired. See
  `CONTRIBUTING.md` for validation and delivery policy.
- RHI 1 is an immediate breaking replacement with no compatibility shim or
  parallel public API.
- Required and optional features are negotiated explicitly; unsupported work
  never reports simulated success.
- Resource creation and queue submission are thread-safe. Command pools,
  recording command lists, active encoders, and descriptor-arena mutation are
  single-thread-owned.
- Dependencies are backend-optional and pinned. Bundled checked-out source is
  the reproducible default; configure performs no network fetches.
- Backend-platform maturity advances only through objective native evidence.
  Shared contract tests and compilation are necessary but not support claims.
- C++20 and CMake remain canonical. No C ABI is published in RHI 1.

## Validation And References

- `README.md` contains build and consumption guidance.
- `docs/architecture.md` defines module and host boundaries.
- `docs/rhi1/` defines the RHI 1 contract, dependencies, shader packages, and
  maturity matrix.
- `docs/roadmap.md` is the active issue-aligned program sequence.
- `docs/planning.md` defines the durable issue, Project, and decision-tracking
  workflow.
- `docs/adr/0011-rhi-1-replacement-contract.md` records the breaking decision.
- `docs/handoff.md` is the rolling cross-machine work status and must be
  updated before an AI-authored commit.

## Next Meaningful Direction

The practical Apple Silicon baseline is established for Metal, Vulkan via
MoltenVK, and OpenGL 4.1 (see the support matrix and handoff for evidence).
Shared Shader mechanisms/tooling (#178/#180), native prepared-TSP Renderer
execution (#179), and optional runtime compilation with safe rebuild adoption
(#181) are completed foundations, not module-completion claims.
**The agreed Shader baseline (#73) is implemented; #193 records final
acceptance.** Route contracts/reflection, portable Metal/GL output, native MSL
source/metallib, HLSL/DXC, native Slang imports, WGSL/Tint and the shared
composition/cache/rebuild audit (#185-#192) are delivered.
The [evidence-linked Shader matrix](src/shader/README.md#completion-scope-and-evidence-73-193)
is the authority for actual routes, profile/API floors, experimental gates and
representational limits, not full upstream-language or cross-product support.
Native Metal/Vulkan/OpenGL 4.1 evidence is local to Apple Silicon; DXIL native
Windows consumption, new Linux/GLES output checks and browser execution remain
separately tracked, not simulated or blocking this baseline.

The accepted next scope is the minimum flexible Renderer (#206), following
[ADR 0012](docs/adr/0012-app-defined-render-data.md): explicit app-defined data,
mesh byte uploads (#207), optional camera/depth helpers and composed example
lighting (#208), then a host-owned Metal window/resize example (#209).
The owner authorized self-review and merging of that stack (ADR PR #205
included). GUI-readiness primitives (#213) follow it: clip rectangles,
sampled-texture upload/update, streamed per-frame geometry and frames-in-flight
pacing. Renderer remains independent of ECS and uses Shader directly.
App-defined transforms, light/material semantics and buffer packing are not
mandatory Renderer schemas. Pass transitions for render-to-texture and stable
native MSL (#215) complete the pre-GUI Renderer work.

The next direction is the GUI module (#217). Its architecture is accepted in
[ADR 0013](docs/adr/0013-truffle-gui-architecture.md): a persistent element
tree that recomputes style, layout and shapes only where something changes,
plus optional Truffle GUI Dock. #217 is decomposed into sub-issues #221-#235;
`Truffle::GUI` provides the element tree and invalidation model (#221) and
incremental measure/arrange layout (#222) and persistent shapes drawn
through the Renderer with a prepared GUI program (#223), and a host boundary
for input, platform services and on-demand frame scheduling (#224). Assets, a general material
system and later application work are not started; the owner reviews the
Renderer architecture later at their discretion, without committing to phases.
Use signed issue-scoped phases, immediate pushes and reviewed merge-commit PRs.
Other backends/platforms, Phase 6 advanced features, and Phase 7 hardening are
deferred backlog (see `docs/roadmap.md`), not abandoned. Promote no pair beyond
its recorded evidence.

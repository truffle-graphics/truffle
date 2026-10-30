# Handoff

This is the rolling execution handoff, not an implementation transcript.
`PROJECT_CONTEXT.md` owns durable direction; `docs/roadmap.md` and the GitHub
issues/Project own accepted work. Earlier phase details remain in their PRs,
the support matrix and Git history.

## Current Delivery: Minimum Renderer (#206)

Three owner-approved signed/pushed slices remain review-only PRs, never merged
automatically: data/mesh #207, optional view/depth #208, host-window example
#209. They stack on ADR PR #205. The minimum Renderer is the only accepted next
scope; GUI and later applications are not planned here.

#207 (PR #210) adds explicit, reflection-checked vertex layouts, owned immutable
mesh byte uploads and batch-range validation. Renderer owns those mechanisms; Shader/RHI
retain their lower-layer responsibilities. ECS semantics, buffer-member packing
and lighting remain application-owned. Separate mesh upload, layout validation,
and orchestration files keep ownership cohesive. Nine focused Renderer, EnCom
adapter and frame-graph tests pass with warnings-as-errors, including native
Metal uploaded indexed-mesh exact 256-pixel proof under validation. The final
native rerun also covers indexed indirect buffer binding and rejection of
vertex-layout-changing replacement. Architecture guard and whitespace checks pass.

#208 (PR #211) adds optional camera math and depth allocation/transition helpers. The
shared minimum-renderer application fixture owns its vertex/instance/view/light
types, composed MSL shader chunks and explicit completion fence. Renderer does
not acquire those semantics. View/depth contract tests and the Metal offscreen
proof pass. The latter checks all pixels for occlusion in either instance
order, app properties, light direction/intensity, quaternion interpretation,
camera translation, perspective, and resized 32x24 targets under validation.

#209 adds a real Cocoa/CAMetalLayer host example, two rotating lit cubes and
swapchain-pass depth/clear preservation. The host owns window/input/backing
size/resize/out-of-date recovery; Renderer owns no platform window policy.
The smoke presents 12 frames with two actual drawable/depth resizes. The same
cube geometry is checked against an exact spatial offscreen reference.
Scene completion fencing also runs after recoverable presentation errors so
CPU updates cannot race an earlier successful submission. Separate scene
data/shading and native window-host files remain cohesive example owners.

Final focused evidence: 12/12 native-enabled Renderer/EnCom/frame/window tests,
plus 4/4 compiler-disabled data/view/source-program/prepared-Metal tests.
The installed compiler-off package consumer also builds and runs with the new
Mesh/layout/view/depth public headers and functions.
Warnings-as-errors builds pass. The architecture guard checks 17 production
targets, 25 dependency declarations and 104 public-header imports. No backend
maturity, cross-platform claim or version bump is implied.

## Render Data Direction (#204)

[ADR 0012](adr/0012-app-defined-render-data.md) records the accepted
direction: fixed renderer concepts (mesh, buffer, texture, program, pass,
view/camera, draw; optional Renderer camera helpers) stay fixed, while
per-object data, its interpretation, lights and materials are
application-defined, matched to Shader reflection and uploaded unchanged.
ECS/scene types stay outside the Renderer. The legacy `ChannelKind`
and `renderer/components.hpp` remain unchanged for existing integrations; the
new path does not require them. The owner will review the architecture after
the minimum implementation, rather than committing to future phases.

## Previous Delivery: Shader Completion (#73, #193)

The agreed Shader baseline is implemented and accepted. #193 and parent #73
are closed/Project Done after PR #203 merged into develop. That delivery did
not add Renderer features; the separately accepted work above now does.

| Phase | Accepted delivery |
| --- | --- |
| Shared library/tools and direct Renderer use (#178-#181) | Independent Shader, TSP assembly/inspection, prepared programs and `SourceProgram` compile/rebuild adoption |
| Route/target contracts (#185) | PR #194 |
| Canonical reflection/workgroup specialization (#186) | PRs #195/#196 |
| Portable Metal/GL generation (#187) | PR #197 |
| Native MSL source/offline libraries (#188) | PR #201, merge `f833cafffd56753e0b4d74be23f2607ca6afc2ea` |
| HLSL/DXC (#189) | PR #199, public acceptance merge `5852204c9f49c933c6a6fbbd6e1b3b560ac93bac` |
| Slang/native modules (#190) | PR #200, merge `990f7e679f41f1dc9d55c7d5a31e3acbbac87381` |
| WGSL/Tint (#191) | PR #198, merge `f2079307fd2cd5432d465c083cc0c416e0b1ec5f` |
| Shared composition/cache/rebuild/tool audit (#192) | PR #202, merge `0e1bcb9ffc9fd11797a31eee02f8abb2fb590e88` |
| Final evidence/license/export/status audit (#193) | PR #203, merge `cf6836c2c1fb38097388b6e6b9d4ce7aabab58b2`; no remaining accepted Shader implementation phase |

The [evidence-linked Shader matrix](../src/shader/README.md#completion-scope-and-evidence-73-193)
defines supported routes, profile/API floors, experimental gates and reflection
limits. Completion is not every upstream language feature, every conversion
pair, or backend conformance. Existing explicit unsupported combinations are
not relabeled as implemented.

## Ownership And Consumer Contract

- Dependency direction is Renderer -> Shader -> RHI -> Base, without an
  adapter module. RHI/backends never depend on Shader or compiler tools.
- Shader owns source resolution/composition, requested configurations,
  optional compilation, canonical reflection, dependencies, instance-owned
  in-memory cache and explicitly triggered rebuilds.
- Renderer owns GPU resources, compatibility and safe publication. Prepared
  TSP and in-memory compilation results share the same RHI package contract.
- Compiler adapters remain individually optional. Disabling shaderc does not
  disable library compilation; disabling all adapters leaves installed
  Shader/Renderer/RHI consumers compiler-free.
- No custom shader language, mandatory watcher, hidden service, persistent
  cache policy or material/asset system was introduced by Shader completion.
- TSP remains an evolving unreleased `1.0` draft. No version bump, migration,
  compatibility alias or legacy reader is part of this work.

## Native MSL Acceptance And Review Disposition

Native MSL uses ordered `sourceUnits` plus the root and a typed canonical
`interfaceDeclaration`; Metal owns preprocessing. Runtime source reflection
requires macOS 26+ and a Metal device. This does not raise prepared-TSP
consumption's runtime floor.

Observable stage, vertex inputs, resource access/kind/count/size/native slots
and function constants are checked. Logical slots, unavailable IO/interpolation,
push layout, defaults and workgroup details remain explicit caller assertions.
Reserved push buffer 30 must be read-only. Arbitrary project includes are not
promised by the in-process Metal API; select reusable units explicitly.

Offline Metal dependency scanning runs before every cache lookup and rejects
active dependencies outside the installed Metal Toolchain. The installed
compiler/AIR linker preserves reflection; the exact emitted metallib is loaded
and reflected before publication. Source/offline target-conditional mismatch,
newly appearing include dependencies, concurrent compiler processes, continued
directives, writable pushes and ambiguous CLI declarations have regressions.

Cohesion review: `metal_adapter.mm` owns one native request's assembly,
compile/reflection/declaration validation and publication lifecycle.
Distinct tool discovery, dependency scanning and process/pipe/reaping details
are extracted to the private Shader-owned `metal_offline_toolchain` helper.
The length signal is not a God-file verdict or reason for arbitrary splitting.

Native evidence used Apple M3 Pro, macOS 27.0.1, Xcode 27.2 and Apple Metal
compiler 32023.922 / installed Metal Toolchain. Both composed source and offline
libraries produced 256 exact RGBA `(64,64,32,255)` pixels and compute values
`10,12,14,16` (defaults) / `15,18,21,24` (overrides). Toolchain layout/version
requirements are explicit, not guarantees for arbitrary SDKs or older systems.

## Shared Mechanisms And Final Packaging Audit

The standalone `examples/shader_workflow` links only `Truffle::Shader`,
in-tree or installed. It exercises three-source native GLSL composition,
explicit configurations, cache reuse, compile/reflection rejection and cached
recovery without a backend, Renderer, GPU or shaderc.

Native compiler regressions reject observable reflection mismatch and
overlapping canonical push ranges, repeat failed requests without cache
success, recover valid cached output and retain loadable last-good packages
after cache clearing/compiler destruction. Failures never masquerade as
successful last-good results.

The #193 install audit found glslang's license missing from its exported
compiler closure. Shader now installs the pinned `LICENSE.txt` under the
configured data directory; installed-consumer smoke compares its SHA-256 with
the source notice. Other enabled provider/nested notices were checked.
[Dependency policy](rhi1/dependencies.md#shader-tooling) and
[Slang source closure](shader-slang.md) record exact pins, licenses, offline
acquisition and update requirements. Apple frameworks/toolchains are host
prerequisites and are not redistributed.

CLI/library compilation mechanisms are shared. Compilation/reflection/package
failures occur before opening an existing CLI output; this is not a general
filesystem transaction or crash-recovery guarantee.

## Validation Evidence

- Accumulated native/provider acceptance: 29 focused enabled checks with
  warnings-as-errors, plus nine exact final-MSL compiler-off checks.
- Integrated #192: seven enabled workflow/compiler/route/native/CLI/Renderer/
  installed-consumer checks and eight compiler-off package/tool/consumer checks.
  Enabled and disabled standalone installed examples build; unavailable
  compilation returns a real nonzero failure.
- Final install correction: enabled and compiler-disabled installed-consumer
  smoke each pass, including exact glslang notice comparison when enabled.
  The enabled install also passes with a custom data directory. No new
  compiler source build or full platform matrix was needed.
- Architecture guard: 17 production targets, 25 target dependency declarations,
  95 public-header imports. No reverse dependency or public-contract change.
- Compiler-off installed prepared consumer links only system C++/OS libraries;
  enabled installed shaderc executes without loader override variables.
- Changed-document local link targets and `git diff --check` pass.

Tests and PR evidence are local Apple Silicon results. No hosted workflows,
automatic AI reviews, cross-platform promotion or version bump were requested.

## Other Baselines And Outstanding Evidence

Metal macOS (#69, PR #174), Vulkan/MoltenVK (#56, PR #170), and macOS OpenGL
4.1 presentation/synchronization (#61/#63 macOS parts, PR #176) provide the
practical Apple Silicon baseline. They remain `native_smoke`, not full
cross-backend conformance; Null is validation-only.
See the [support matrix](rhi1/support-matrix.md) for precise native receipts.

- New Shader DXIL consumption on Windows/D3D12: #122.
- Current provider/output revalidation on Linux Vulkan: #124.
- Generated GL/GLES output on Linux, including native ES 300: #125.
- Browser WGSL/WebGPU output consumption: #123, after its backend exists.
- Vulkan Windows (#58), remaining desktop presentation/context work (#61/#63),
  Apple mobile lanes (#71/#72), and MoltenVK packaging/iOS (#172) stay tracked.
- Metal timestamp/occlusion pools remain #175; asynchronous submission,
  allocation, recovery, advanced GPU features and full conformance remain
  separate Phase 6/7 work. Synchronous correctness is not throughput evidence.
- macOS targets Apple Silicon, not Intel Macs (#171). Physical GPU removal is
  not an Apple Silicon requirement; device-loss fault injection is not
  physical-removal evidence.

Other-platform verification does not block this Shader baseline. Old backend
smoke runs do not establish native use of the newly added Shader outputs.

## Resume And Delivery Rules

1. Review the minimum Renderer stack (#206-#209), beginning with ADR PR #205
   then #210 and #211 and the host-window PR. Do not merge automatically.
   The implementation and evidence are local Apple Silicon; architecture
   review remains open. Do not start GUI or application phases without scope.
2. Preserve native worktrees/branches/build assets; this audit authorizes no
   branch deletion or history rewriting. Use signed issue-scoped commits,
   immediate same-branch pushes and reviewed normal merge commits.
3. Keep validation local and affected-area focused. Hosted Build/Release and
   cloud/AI companions remain off unless explicitly requested (#131).
4. Runner setup (#165) and retained action-runtime modernization (#132) remain
   separate deferred work. No release or stable-branch promotion is implied.

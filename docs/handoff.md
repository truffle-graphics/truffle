# Handoff

This is the rolling execution handoff, not an implementation transcript.
`PROJECT_CONTEXT.md` owns durable direction; `docs/roadmap.md` and the GitHub
issues/Project own accepted work. Earlier phase details remain in their PRs,
the support matrix and Git history.

## Current Delivery: Shader Completion (#73, #193)

The agreed Shader baseline is implemented. Final acceptance is recorded in
#193 and its reviewed merge-commit PR; reconcile that issue and parent #73
before resuming Renderer feature development. No Renderer feature is added by
this delivery.

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
| Final evidence/license/export/status audit (#193) | This delivery; no remaining accepted Shader implementation phase |

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
  cache policy, material/asset system or new Renderer feature was introduced.
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

1. Verify #193/#73 acceptance and synchronized Project status. Then select a
   focused Renderer issue; materials, lighting and texture/asset management
   still require their own accepted scope.
2. Preserve native worktrees/branches/build assets; this audit authorizes no
   branch deletion or history rewriting. Use signed issue-scoped commits,
   immediate same-branch pushes and reviewed normal merge commits.
3. Keep validation local and affected-area focused. Hosted Build/Release and
   cloud/AI companions remain off unless explicitly requested (#131).
4. Runner setup (#165) and retained action-runtime modernization (#132) remain
   separate deferred work. No release or stable-branch promotion is implied.

# Handoff

Use this document as Truffle's rolling cross-machine handoff. Keep it useful to
a human resuming work and to a fresh AI session reading the repository.

## WGSL/Tint source routes — issue #191 (merged in PR #198)

- Integration update: normally merged `origin/develop` at
  `5852204c9f49c933c6a6fbbd6e1b3b560ac93bac` (HLSL PR #199) into this
  branch without rebasing. Shared routes/API/CLI retain HLSL shader-model
  fields, DXIL routes with no Vulkan intermediate, native output gates and
  all WGSL routes. Tint reuses `compiler_inputs.hpp` key-field encoding and
  `translation_provenance()`; shared GLSL validation remains used by GLSL/HLSL
  outputs (Tint exposes no GLSL output route). Canonical SPIRV-Headers
  reflection is compiled once for any enabled SPIR-V adapter.
- Integration verification reused the existing compiler builds: Tint-only
  with glslang OFF passed 12 focused tests; GLSL/Tint enabled integration
  passed the same 12, including native exact Vulkan/Metal output, real
  glslang-intermediate→WGSL conversion, CLI/library equality and installed
  consumers. Warnings-as-errors remains enabled. DXC stayed OFF in these
  WGSL builds: no second DXC source build or new HLSL execution claim.
  Seven focused compiler-off route/CLI/Renderer/installed-consumer integration
  checks also passed. HLSL's previously merged evidence remains with PR #199.
- Implemented in the dedicated `feat/shader-wgsl` branch: pinned optional real
  in-process Tint (Dawn `46b4670bc67cb4f6d34f6ce6a46ba7e1d6059abf`,
  BSD-3-Clause) and Dawn's pinned Abseil
  `04dc59d2c83238cb1fcb49083e5e416643a899ce` (Apache-2.0). No configure
  downloads, source patches, system fallback, Dawn/WebGPU/browser/GUI runtime,
  tests, compiler executable or irrelevant writer build.
- Real advertised vertex/fragment/compute routes: WGSL→Vulkan 1.1/SPIR-V 1.3,
  WGSL→WGSL, SPIR-V→WGSL and (with existing Cross) WGSL→macOS MSL 2.1.
  WGSL output is experimentally gated in compile/load/CLI, reparsed by Tint,
  and recompiled in tests. No direct source-language-to-WGSL claim beyond
  WGSL and the explicit SPIR-V intermediate route.
- Canonical emitted-SPIR-V reflection, source identity/hash, pinned compiler
  provenance and cache/rebuild contracts are reused. Native entry/stage
  selection uses Tint IR transforms. WGSL has no preprocessor/imports:
  defines/include roots/resolver, debug-info requests and selected pipeline
  overrides receive typed rejection, never silent substitution or omission.
  Host-composed ordinary WGSL functions/constants are supported as one
  exact-byte dependency; no new shader language.
- Existing pinned Tools/Headers compile and validate Tint's real reader/writer.
  Tint's reader legitimately requires Tools optimizer internals; the used
  optimizer static closure is exported only with Tint. Exact pair differences
  from Dawn DEPS and update risks are documented in the dependency policy.
  Used Tint/Abseil static archives are exported privately without public Tint
  headers; all-off installed prepared consumers have no compiler link closure.
- Native Apple M3 Pro evidence: active Vulkan validation/MoltenVK and enabled
  Metal API/GPU validation each read back all 256 resource-backed triangle
  pixels as RGBA `(64,64,32,255)` and all four compute words as
  `(12,13,14,15)`. Tint's automatic point-size output is disabled explicitly:
  otherwise triangle MSL fails native pipeline creation. No point-topology,
  WebGPU execution or backend maturity promotion is claimed.
- Focused local evidence covers Tint-only builds (glslang OFF), combined
  glslang/Tint builds, emitted WGSL and SPIR-V import, exact canonical
  graphics/compute bindings/IO, invalid source/entry/stage/options,
  source-edit cache/failure/recovery, CLI/library exact package equality,
  experimental loading, enabled installation/static consumers, and
  compiler-off route/package/Renderer/prepared-consumer isolation.
- Delivery is a signed issue-scoped commit and immediate feature-branch push,
  then a PR into `develop`; parent owns review/acceptance/merge. Do not close
  #191 or merge independently. Shader-first remains in effect; Renderer
  feature work stays paused. TSP remains unreleased draft 1.0.

AI work that reaches a commit must update this handoff before that commit.
Promote durable decisions into project context, architecture, roadmap, or ADRs
rather than growing a historical transcript here.

## Native Slang Modules (#190, Ready For Integration Review)

- **Owner/boundary:** optional in-process compilation remains in Shader; the
  CLI uses that library. Native imports/includes and Slang component linking
  compose reusable modules, emitting exactly one requested entry/configuration.
  Canonical emitted-SPIR-V reflection remains RHI-owned metadata; TSP stays 1.0.
  No Shader→backend/Renderer dependency or backend maturity promotion.
- **Actual matrix:** vertex/fragment/compute → SPIR-V 1.3 / Vulkan 1.1 only,
  independently validated with the existing pinned SPIRV-Tools. No Slang
  Metal/GL/WGSL/DXIL or advanced-stage route is advertised. `sourceUnits` is the
  agreed native-MSL-only field. `Compiler::compile` centrally rejects it for
  every non-MSL language before adapter discovery/resolution, including Tint
  WGSL/SPIR-V inputs and compiler-off requests; private duplicate guards are removed.
- **Pins/source policy:** clean upstream Slang `v2026.12.1`,
  `90b8e77eafdfa0aabbbc48f0e86f6cfbde4cb8f8`, Apache-2.0 WITH LLVM-exception.
  Exact audited nested source pins/licenses and initialization commands are in
  `docs/shader-slang.md`. Configure/build never acquire dependencies; no binary
  SDK/system fallback or local upstream patch. Embedded core source/module and
  direct SPIR-V need no LLVM/DXC/glslang/optimizer runtime plugins. The selected
  upstream pin includes the optional-downstream-loading fix absent in the
  initially audited 2025.10.1. Install exports only the source-built compiler
  dylib and notices, not an upstream SDK.
- **Sources/cache/failure:** immutable per-attempt source snapshots include
  transitive native import/include identities and hashes. Fresh native sessions
  resolve/link before cache lookup, so dependency/configuration/debug changes
  cannot return stale success. Missing/cyclic imports, invalid source/entry,
  unsupported targets and failed rebuilds preserve `last_good()`; failed CLI
  compilation retains the previous output file. Native Slang diagnostics and
  source-language semantics are not replaced with a custom parser.
- **Native evidence, Apple M3 Pro/MoltenVK:** active Vulkan validation; native
  vertex/fragment yields exactly 256 RGBA `(78,0,0,255)` pixels. Imported compute
  `QUALITY=3/4` yields `307..314` / `407..414`, preserving the ninth sentinel.
  Slang `SV_VertexID` subtracts BaseVertex and emits `DrawParameters`; canonical
  reflection now explicitly rejects that unmodeled device capability.
  Existing native `SV_VulkanVertexID` is used instead, with no backend feature
  changes or silent source rewriting. Negative regression covers the rejection.
- **Focused final implementation checks:** Release Ninja, explicit Xcode SDK,
  warnings-as-errors and builds limited to `--parallel 2`. Combined GLSL+Slang
  **16/16**, Slang-only/glslang-off **10/10**, compiler-off **11/11** pass after
  the WGSL develop merge. Checks
  include native proof, one emitted entry/all stages, multilevel imports/includes,
  content/configuration invalidation, cycles/missing-source recovery, concurrent
  compiler instances, CLI/library byte equality, and installed consumers.
  Compiler-off installed Shader/prepared Renderer/RHI consumers have no optional
  compiler closure. Architecture guard and `git diff --check` pass. Other hosts
  and Slang output targets are not verified or claimed.
- **Integration:** shared reflection uses canonical SPIRV-Headers rather than a
  glslang-private include and is built for independently enabled adapters.
  Headers license notices are retained even for glslang-only/validator-off installs.
  Dispatch/discovery and test gates no longer assume glslang is the only adapter.
  Slang uses no Cross outputs, so it adds no competing generated-GLSL validator
  or translation provenance helper. Normal merge of develop
  `f2079307fd2cd5432d465c083cc0c416e0b1ec5f` preserves independent DXC/Tint
  routes/options, HLSL Shader Model fields, experimental WGSL gates and native
  empty-intermediate CLI/consumer handling. Canonical reflection is compiled
  once for any enabled SPIR-V adapter; Tint needs no glslang.
  Slang now reuses `compiler_inputs.hpp` define/key helpers. Only existing
  affected Slang/GLSL/off builds are revalidated; no second DXC or Tint build.
  Shared rejection tests cover all non-MSL languages in enabled/off builds.
  WGSL's regression runs before its disabled-adapter check and additionally
  covers every enabled stage and valid SPIR-V→WGSL inputs when Tint is enabled.
  Local HLSL/WGSL tests exercise disabled-adapter paths, not new native proofs.
  Enabled DXC/Tint checks use the parent's existing compiler builds.
- **Installed-tool correction:** the shared-provider CLI runtime path now
  covers DXC **or** Slang: macOS `@loader_path`, Linux/Unix `$ORIGIN`, calculated
  between configured bin/lib directories (absolute library destinations remain
  absolute). The install smoke executes the **installed** `--self-test` and
  `--routes` with loader override paths unset, only when
  `TRUFFLE_BUILD_SHADERC=ON`; explicit OFF skips stale installed executables.
  Consumer package discovery also respects the configured library directory.
  Existing Slang-only build verifies nested `tools/native/bin` and
  `runtime/lib64`, with actual LC_RPATH
  `@loader_path/../../../runtime/lib64` and successful installed execution.
  Compiler-off `shaderc=OFF` regression retains a real stale installed tool and
  proves it is skipped; default enabled/off installs are checked too.
  No new DXC/Tint compiler source build or Linux execution claim. Parent reruns
  DXC-only installed execution using its existing build.
- **Next:** parent owns
  independent review, merge, issue acceptance and any later route expansion.

## HLSL Compilation (#189, Merged In PR #199)

- **Owner/boundary:** Shader owns the optional in-process DXC adapter, native
  include/define preprocessing, dependencies, caching, validation and emitted
  reflection. shaderc is a client of that same API. Dependencies remain private
  DXC/SPIRV-Tools and optional glslang/SPIRV-Cross above RHI/Base; native backends
  are linked by tests only. No architectural boundary change, Renderer feature,
  new language, subprocess runtime compiler, watcher, or TSP version change.
- **Routes:** independently enabled `TRUFFLE_SHADER_ENABLE_DXC` supports HLSL
  2021, Shader Model 6.0 vertex/fragment/compute, Vulkan 1.0/SPIR-V 1.0 and
  Vulkan 1.1/SPIR-V 1.3, plus native DXIL without a Vulkan intermediate.
  `HlslShaderModel`, request/route metadata and `--hlsl-shader-model 6.0` expose
  that bounded contract. Unsupported profiles/configurations fail explicitly.
  Optional portable emission reuses #187: experimental macOS MSL 2.1 for all
  three stages, GL 410 core/ES 300 for vertex/fragment.
- **Dependency audit:** official DXC `v1.9.2609`, exact revision
  `01b62ad47db7dee3dd33f90e7339cf9e860b3f93`, built offline in an isolated
  Release/Ninja child build. Exact nested DirectX-Headers/SPIRV-Headers/
  SPIRV-Tools pins and root/nested licenses are recorded in
  `docs/rhi1/dependencies.md`; no local patches or implicit downloads.
  Official binaries do not cover Apple Silicon, so this host uses the supported
  source build. Only `dxcompiler` is built; it links system C/C++ libraries.
- **Composition/cache:** native DXC preprocessing runs before every lookup.
  Its callback supplies searched candidate paths, not an original includer or
  quote/system distinction; the shared resolver receives an empty includer and
  `system=false`. Resolved content is frozen for the actual compile. Stable
  identities, transitive content hashes, sorted defines, profiles, options,
  compiler revision and optional translation provenance determine cache identity.
  Native guards/macros remain compiler-owned; failed rebuilds retain the
  previous published result and expose diagnostics.
- **Reflection/validation:** SPIR-V uses the shared real validator/reflector.
  DXIL uses DXC's real validator and emitted DXIL reflection plus checked PSV
  signature metadata, never a second SPIR-V compilation. Fixtures prove
  target-predefined macros can change bindings and malformed DXIL is rejected.
  User DXIL IO is bounded to `TEXCOORDn` and fragment `SV_Targetn`; unsupported
  register-class collisions, unbounded descriptors, typed buffers and
  append/consume/counter resources fail rather than receiving invented remaps.
  Structured/raw storage minima reflect native stride/four-byte access.
- **Focused evidence, Apple Silicon:** final combined Release/warnings-as-errors
  configuration passes **19/19** with
  `ctest --test-dir build/shader-hlsl -R 'truffle_shader|truffle_renderer_source_program_tests' --output-on-failure --parallel 2`.
  This includes all 23 HLSL stage/target/intermediate configurations with
  byte-identical CLI/library packages, transitive/guarded includes, defines,
  dependency invalidation, failed rebuild retention, reflection, real validation,
  existing GLSL/portable regressions and installed Shader/Renderer consumers.
  DXC-only passes **8/8** focused tests and separately installed consumers without
  glslang/Cross. Compiler-off installed Shader/Renderer consumers pass; their
  link commands contain no optional compiler libraries.
- **Native evidence:** unchanged serialized HLSL packages render all 256 pixels
  exactly RGBA `(64,128,255,255)` on Vulkan/MoltenVK, Metal and macOS GL 4.1.
  Vulkan/Metal compute writes `107..110`, retaining four `0xdeadbeef` tail values.
  Vulkan validation and Metal API/GPU validation are active. DXIL validation is
  real on this host but is not Windows/D3D12 driver execution; GLES has compiler
  validation, not native evidence. Other-host validation remains separately
  tracked and is not required to pretend D3D12 runs on this Mac.
- **Cohesion:** the adapter coordinates compilation/package/cache lifecycle;
  DXIL validation/reflection and COM ownership are separate private helpers.
  Shared define/key normalization and existing generated-GLSL validation/Cross
  provenance are reused rather than copied. Architecture and whitespace guards
  pass. One issue-scoped signed commit/paired push is the approved delivery unit.
- **Next:** review and integrate #189, then reconcile the independently developed
  native MSL (#188), Slang (#190), WGSL (#191), shared-mechanism audit (#192) and
  final completion audit (#193). Shader is not complete; Renderer features stay
  paused. Source-unit contracts introduced by #188 must reject unsupported
  adapters explicitly during integration.

## Portable Shader Targets (#187, Merged Baseline)

- **Independent review fixes on PR #197:** MSL translation now checks all seven
  upstream auxiliary-resource feedback APIs **after** compilation and rejects
  unsupported helper contracts before publishing source. Runtime buffer-tail
  `.length()` specifically needs an unbound size table (default buffer 25,
  overlapping allocated descriptors); no fake remap is added. Swizzle/view/
  dispatch/capture helpers are rejected too; dynamic helper offsets and
  tessellation/capture options are not enabled. Runtime-tail indexing remains
  supported and executes in Vulkan/Metal compute; Vulkan `.length()` still
  compiles. Translator provenance advances to `truffle-cross=2`.
- **Explicit resource authority:** RHI's `ShaderDesc::resourceBindingsComplete`
  defaults false; package selection sets it only for `generated` variants.
  Complete empty reflection means no resources; optional/empty low-level or
  precompiled/native-override metadata does **not** imply that. Metal snapshots
  the per-stage authority with reflection/remaps and skips genuinely absent
  resources despite broad layout visibility. Existing native fallback remains.
  No new TSP wire field/version or compiler/backend dependency is needed.
- **Review regression evidence:** new Metal native graphics uses group 0,
  uniform binding 0, real vertex stream 0 and `ShaderStageMask::all`. Vertex/group
  binding in **both orders** reads all 256 pixels exactly `(64,64,32,255)` from
  unchanged generated packages. Low-level native compute with empty resource
  metadata retains real storage-buffer binding and specialized readback.
  The new `.length()` unit regression was confirmed failing before the fix.
- **Focused review validation:** existing Ninja Debug/SDK/warnings-as-errors
  builds, `--parallel 4`; translator-on **7/7** portable unit/native, CLI
  equivalence, package, Metal backend/conformance and Renderer–Metal tests pass.
  Compiler-off **6/6** compiler/route/package, Metal and installed Shader/
  Renderer/RHI consumers pass, with no optional compiler names in the installed
  Shader consumer link graph. Previous full focused configuration evidence
  below is the implementation baseline, not a repeated full matrix.
  Metal binding snapshots/encoding remain one backend-owned lifecycle, and RHI
  forwards semantic completeness without owning translation. The larger
  backend/RHI files remain cohesive for these narrowly coupled changes.
- #185/#186 are completed in PRs #194/#195/#196. This bounded compiler-route
  phase uses the existing `feat/shader-portable-targets` checkout, based on
  develop `b269e60bbea1572e51a7b15b1dfdd19b1edc112d`; no other checkout/worktree.
  Parent owns review, merge and tracker acceptance. Shader is **not complete**;
  Renderer feature development remains paused; #188-#193 remain outstanding.
- **Owner/boundary:** Shader owns optional in-process validation/translation,
  shared source/include/define/cache/rebuild orchestration and output ownership.
  RHI owns canonical package semantics and logical/native entry lookup. Shader
  links only RHI/Base plus private optional toolchains; tests alone link native
  backends. No Shader→backend/Renderer or RHI→Shader dependency.
- **Profiles:** GLSL and Vulkan GLSL ES input → SPIR-V, experimental macOS MSL
  2.1 source, GLSL 410 core, or GLSL ES 300. Native routes use the same three
  Vulkan/SPIR-V intermediate configurations. Metal has vertex/fragment/compute;
  GL 410/ES 300 have vertex/fragment only. Unknown profiles/stages and unsupported
  features fail explicitly before claiming success. No native MSL authoring or
  metallib output (#188), HLSL/Slang/WGSL route or higher GLES profile is claimed.
- **Pins/audit:** SPIRV-Cross SDK `1.4.350.1` at
  `1a6169566c73d3da552748fc372fe2bbb856e46e` (Apache-2.0 plus bundled notices);
  SPIRV-Tools `v2026.3` at `b707790a898e44038547df54580022fc1cf89c3d`
  (Apache-2.0), paired Headers `29981f65241605e08b0ede4cfeb999fe3b723c6a`
  (Khronos MIT-style plus bundled notices), selected from glslang's known-good
  pair. Audited C++ validation, remap, combined-sampler and cleansed-entry APIs.
  Cross uses upstream private C++/SPIR-V namespace overrides to prevent header/
  symbol collisions, retains diagnostic exceptions, and adds no optional allocator.
  No local patches. Source preparation was explicit; configure never fetches.
  Clean pins are checked to prevent inaccurate compiler provenance. Install
  exports only the validator static closure plus Cross core/GLSL/MSL configs,
  preserving license texts. See dependency policy for updates/offline builds.
- **Package semantics:** `entryPoint` remains logical selection/agreement;
  `nativeEntryPoint` is explicitly serialized and forwarded unchanged to native
  lookup. Thus Metal's `main0` still agrees logically with SPIR-V/GL `main`.
  Draft TSP stays `1.0`; there are no old readers or migrations. Metal generation
  still requires explicit request **and load** opt-ins; shaderc exposes that
  consent without bypassing package validation. WGSL gating is unchanged.
- **Remaps/limits:** real MSL slots reserve 0..15 for vertex streams and 30 for
  push data. GL retains both logical texture/sampler bindings and maps them to
  the same actual combined uniform/unit; no binding is dropped. Native descriptor
  arrays and storage images are rejected. GL requires statically used resources,
  one-to-one scalar texture/sampler pairs and scalar/vector internal IO; storage,
  push/specialization lowering and extension-only constructs are explicitly
  unsupported in these core profiles. A broader sampling graph needs a shared
  multi-use native binding contract, not fabricated remaps.
- **Coupled correction:** real validation exposed previously accepted nested
  descriptor-array SPIR-V as invalid Vulkan (UniformConstant VUID 04655).
  Reject those shapes in shared reflection even when the validator is disabled;
  retain supported one-dimensional descriptor arrays and nested **buffer data**
  arrays. Positive/negative fixtures and feature docs now reflect that truth.
- **Native evidence, Apple M3 Pro:** unchanged serialized generated packages,
  same portable vertex/fragment source, real texture/sampler/uniform bindings:
  Vulkan/MoltenVK, Metal and OpenGL 4.1 each read exactly 256 RGBA
  `(64,64,32,255)` pixels. Generated Vulkan and Metal compute exercise push data,
  scalar constants and direct local-size IDs; widths 2/3 write `2107..2110` /
  `3113..3118`, leaving sentinel tails untouched. Metal API/GPU validation and
  Vulkan validation are enabled; macOS GL performs native per-call error checks.
  GLES gets real generated-source glslang parse/link validation, **not native
  execution evidence**; higher profiles remain future work.
- **Focused validation:** Ninja Debug, explicit Xcode SDK sysroot,
  warnings-as-errors and `--parallel 4`; no hosted workflows or installed tools.
  - Translator-on `build/shader-portable`: **18/18** Shader/native/CLI/install and package
    consumers pass with the same focused selector below, including all 42 native route/stage/intermediate profile
    configurations and byte-identical CLI/library packages, invalid SPIR-V,
    source/target failures, cache/rebuild preservation, and logical agreement
    with changed native entry names. Native invocation uses
    `ctest --test-dir build/shader-portable -R '^truffle_shader_portable_native_tests$' --output-on-failure -V`.
  - glslang-only `build/renderer-shader`: **15/15** pass with
    `ctest --test-dir build/renderer-shader -R 'truffle_shader|truffle_renderer_(source_program|program)_tests|truffle_package_consumer' --output-on-failure --parallel 4`.
  - All-off `build/renderer-shader-off`: **13/13** pass with the same selector
    plus `|^truffle_metal_tests$`. Installed Shader/prepared Renderer/RHI
    consumers link no glslang, SPIRV-Tools or SPIRV-Cross libraries (link commands
    checked). Assembly and inspection remain available with all compilers off.
  - Architecture guard and `git diff --check` pass. Cohesion: validation and
    translation are separate private Shader helpers; glslang orchestrates their
    calls without owning their internals. Package codec/agreement/selection
    stays in RHI. No unrelated backend or Renderer feature grew.
- **Next:** parent independently reviews/accepts this signed issue-scoped PR and
  its explicit limits, owns merge/tracking, then sequences #188-#193. Do not
  merge, close issues, change Project states or delete this branch here.

## Shader Reflection Contracts (#186, Completed In PR #196)

- #185 is done in PR #194. The first representable reflection phase is merged
  in PR #195 (`8e4c72f`); canonical IO/workgroup contracts are merged in PR #196.
- Both shared-contract extensions were approved in
  [#186 comment 6527097946](https://github.com/truffle-graphics/truffle/issues/186#issuecomment-6527097946).
  TSP is an unreleased development draft: update contracts/codec/fixtures/
  consumers together, retaining container/schema `1` / `1.0`. No version bump,
  legacy reader, alias or migration; earlier development artifacts must be
  rebuilt. Stable format/versioning guarantees begin with release.
- **Canonical full IO:** RHI owns direction on `ShaderInterfaceVariable` and
  `(stage,direction,location)` uniqueness. Both directions/internal varyings
  survive package normalization, cross-target agreement, serialization,
  selection and `ShaderDesc`. Shader's `StageInterface` and duplicate IO vector
  are removed; `Compilation::interfaces()` borrows package metadata. Renderer
  source linkage and prepared RHI pipeline linkage/vertex-attribute checks
  consume those records. Names do not determine agreement. Conventional
  vector aliases and default/perspective/smooth interpolation, including
  reordered known centroid/sample qualifiers, normalize without hiding actual
  type/location/direction/interpolation disagreement.
- **Workgroups:** RHI owns optional X/Y/Z scalar IDs with default required
  extents in reflection/selected shaders. Referenced int32/uint32 defaults
  must be positive and agree; ID zero/shared IDs work. Shader decodes actual
  SPIR-V `WorkgroupSize` composites or `LocalSizeId`, deduplicating only
  consistent scalar IDs. Expression-derived dimensions are explicitly rejected:
  direct axis IDs cannot describe expression dependencies/override semantics.
  There is no interpreter or stale-default fallback.
- RHI's private metadata helper validates package and direct shader metadata
  and resolves typed overrides before explicit pipeline-size comparisons,
  axis/total limits, native creation and retained dispatch dimensions.
  Total-invocation checks cannot wrap. Effective axis defaults are passed as
  typed native values, including to Metal function constants; matching default
  preferred hints follow resolved dimensions, distinct hints stay independent.
  Vulkan/Metal consume those values; GL native-source uniform specialization
  explicitly rejects local-size IDs, and D3D12's existing runtime-specialization
  rejection covers effective defaults too. WebGPU/WebGL2 have no native compute
  path. GL/Windows/WebGPU execution was not run or promoted.
- Shaderc remains a library client and inspects full IO/direction and axis IDs.
  Adapter/cache provenance advances to `truffle-glslang=4`; compiler identity
  and emitted metadata survive round-trip. No Shader→Renderer/backend or
  RHI→Shader dependency, new route, or Renderer feature was introduced.
- **Local validation:** existing Apple Silicon Ninja Debug/SDK-sysroot/
  warnings-as-errors builds; `--parallel 4`, no installs of dependencies or
  hosted workflows/full matrices:
  - Enabled `build/renderer-shader`: **16/16** focused Shader/reflection/package/
    CLI/install, Null contracts, Renderer-source and native Vulkan tests pass,
    including installed Shader/prepared Renderer and whole-package consumers.
  - Disabled `build/renderer-shader-off`: **12/12** focused Shader/package/CLI/
    install, Null contracts, Renderer-source and native Metal tests pass,
    including installed Shader/prepared Renderer and whole-package consumers. Compiler-off
    installed consumers retain only core-library link dependencies.
  - Vulkan/MoltenVK, Apple M3 Pro with validation active: existing exact
    push/scalar/resource-array proof remains green. New unmodified package
    changes local size `2x1x2`→`3x2x2`: two groups write exactly 8/24 values
    `21200..21207` / `22300..22323`, with untouched sentinel tails and correct
    retained preferred/explicit required dimensions. A shared-ID override
    changes both X/Y and writes exactly 16 values `22200..22215`; its tail
    remains untouched too.
  - Native Metal, with `MTL_DEBUG_LAYER=1` (API validation enabled): prepared metadata defaults `2x2x1`→override `3x1x1` match
    real function constants/dispatch; exact `1220..1223` / `1310..1312` values
    and untouched sentinel tails pass. This is not a new Metal compiler route.
  - Architecture guard and `git diff --check` pass. Cohesion: common RHI
    metadata validation/resolution is private and separate from the large RHI
    dispatch file; package codec/agreement stays in RHI; emitted decoding stays
    in Shader. Informational length-review signals do not imply a new framework.
- **Status:** completed by parent acceptance in PR #196; #187 is now the active
  review phase above and #188-#193 remain outstanding.
  Shader overall is not complete and Renderer features remain paused.
  Combined samplers, subpass/texel-buffer bindings, unsized/specialized
  descriptor layouts, component/index packing, extended IO and advanced stages
  retain their explicit documented representational limits; none is silently
  mapped or promoted. Do not merge, close issues or delete the branch here.

## Shader Route And Target Contracts (#185)

- Implemented in `truffle_shader`: `Compiler::routes()` reports only the two
  enabled GLSL/GLSL ES → Vulkan SPIR-V routes, vertex/fragment/compute, with
  three exact target configurations: Vulkan 1.0/SPIR-V 1.0, Vulkan 1.1/SPIR-V
  1.0, and the unchanged default Vulkan 1.1/SPIR-V 1.3. Compiler-disabled
  builds report no routes. `available()` remains an any-source-route
  convenience, not a native device probe.
- Typed `VulkanTarget` settings are checked before source resolution/compiler
  entry. Invalid enum values return `invalid_argument`; unsupported routes,
  stages and configurations return `unsupported`. Results expose the effective
  target. Compiler revision provenance retains the pinned glslang SHA and adds
  adapter identity/effective target versions; both options differentiate cache
  keys and survive TSP serialization. RHI/TSP schema is unchanged.
- Shaderc is a client: `--routes`, `--vulkan-version`, `--spirv-version`, and
  existing `--inspect` expose the shared contract. Its duplicated hard-coded
  language/target rejection was removed. Prepared assembly remains available
  without any compiler and rejects compilation-only settings.
- Ownership/cohesion reviewed: Shader core owns capability/configuration
  validation; its private glslang adapter owns compilation/reflection and
  target translation. Shaderc owns argument parsing/reporting, not route
  policy. Dependencies remain Renderer → Shader → RHI → Base, with only
  optional private compiler libraries under Shader. No backend, adapter module,
  Renderer feature, watcher/service, or new shader language was added.
- Focused local Apple Silicon validation uses the existing Ninja Debug builds,
  SDK sysroot, warnings-as-errors and `--parallel 4`. Shaderc was enabled in
  both build directories to exercise CLI-on/compiler-on and CLI-on/compiler-off:
  - `ctest --test-dir build/renderer-shader -R
    'truffle_shader|shaderc|truffle_renderer_(source_program|program|vulkan_source)_tests'
    --output-on-failure`: 12/12 passed.
  - `ctest --test-dir build/renderer-shader-off -R
    'truffle_shader|shaderc|truffle_renderer_(source_program|program)_tests'
    --output-on-failure`: 9/9 passed.
  - Library and CLI cover all 18 language/stage/configuration combinations,
    unsupported language/target combinations, invalid settings, exact emitted
    SPIR-V header versions, independent target-option cache identity, repeated
    hits, default equivalence, and persisted target provenance.
  - Installed Shader/Renderer consumers pass in both builds. Disabled consumer
    Ninja link commands contain Shader/RHI/Base (plus Renderer when requested)
    only, with no compiler link dependencies.
  - Existing Vulkan/MoltenVK `SourceProgram` regression passes with validation
    and exact output through rebuild/cache/failure/recovery; no new native
    evidence is claimed for the additional version configurations.
  - Architecture guard and `git diff --check` pass. No dependencies installed,
    full platform matrix or hosted workflows run.
- **Status:** merged in PR #194. Shader is
  not complete: #186 reflection contracts are merged; review #187 portable native outputs, then #188
  MSL/Metal libraries, #189 HLSL, #190 Slang/native imports, #191 WGSL, #192
  shared behavior, and #193 final evidence audit under #73. Renderer features
  (including materials, lighting, texture/asset management) remain paused;
  existing Renderer use is limited to Shader regression/native-output evidence
  and directly required fixes. Continue signed phased commits, immediate pushes
  and reviewed merge-commit PR delivery.

## Renderer Source Programs (#181)

- Renderer now links `truffle_shader` directly (Renderer → Shader → RHI →
  Base; no adapter module, RHI unchanged). `renderer::SourceProgram` compiles
  the requested vertex/fragment configuration with `shader::Compiler`,
  consumes `Compilation` packages in memory, validates the stage link and
  pipeline vertex inputs, and adopts the result via `PipelineCache`
  stage/commit. Failures (compile/include, interface, layout, native pipeline)
  keep the last working program; unchanged rebuilds are cache-hit no-ops.
  `PipelineCache::published` exposes the slot pipeline for change detection.
  The host triggers rebuilds; there is no watcher or service.
- With `TRUFFLE_SHADER_ENABLE_GLSLANG=OFF`, Renderer builds and runs prepared
  TSPs and `rebuild` returns `unsupported`; the installed consumer verifies a
  glslang-free Renderer link line. Compiler-enabled installs carry glslang in
  Renderer's link closure.
- Verification on Apple M3 Pro: enabled build (Vulkan + MoltenVK 1.4.2 with
  validation, glslang) passes 13/13 focused Renderer/Shader/Vulkan/consumer
  tests, including `truffle_renderer_vulkan_source_tests` with exact readbacks
  (cache hit, include change red→blue, recorded old list stays red, three
  failed rebuilds keep blue, recovery to green). Disabled build (Metal + Null,
  Metal validation) passes 12/12 focused tests. Architecture guard and diff
  checks pass.
- Limitations: runtime GLSL→SPIR-V evidence is Vulkan only (Metal consumes
  prepared MSL); the Vulkan source test uses layout mismatch rather than a
  genuine native pipeline-creation failure. Stale `TRUFFLE_SHADERC_ENABLE_GLSLANG`
  doc references were corrected. Next: further compiler routes (#73).

## Shader Review Follow-up (#183)

- Merged current `origin/develop` (including #182), preserving both Shader and
  Renderer handoff sections and their independently registered test targets.
- Review identified that successful compiler logs can exceed TSP's 4096-byte
  per-diagnostic limit. The Shader adapter now truncates only the package
  diagnostic, retaining the compilation result's complete bounded log.
  Regression coverage exercises many warnings, successful rebuild/cache reuse,
  and CLI compilation. Include roots are documented as search paths, not an
  access-control boundary.
- Confirmed the new 100-warning regression fails before the fix with the
  4096-byte package-limit error. Afterward, focused enabled tests pass 9/9
  (including native Vulkan, merged Renderer programs and installed consumers);
  compiler-disabled tests pass 6/6. Both builds retain Ninja Debug, SDK sysroot,
  warnings-as-errors and `--parallel 4`. Architecture guard and diff checks pass.

## Shader Tools And TSP Delivery (#180)

- `truffle-shaderc` now calls the same `Truffle::Shader` adapter as runtime
  clients; CLI-owned compiler implementation and compiler-specific CLI option
  are removed. Enable `TRUFFLE_SHADER_ENABLE_GLSLANG` independently of
  `TRUFFLE_BUILD_SHADERC`. Assembly still works with all compilers disabled.
- Repeated `--include-root DIR` and `--define NAME[=VALUE]` configure source
  compilation. Compilation computes real dependency hashes/compiler identity;
  optional `--source-hash` verifies the root. Inspection shows dependencies,
  configuration names and reflected resource/IO/workgroup counts. Generated
  CLI and library packages are tested byte-for-byte equal. Compiled variants
  can be appended without enumerating other configurations.
- Shader package documentation, native fixture paths and presets now use the
  Truffle Shader Package (TSP, `.tsp`) name and shared-library option. The C++
  `truffle::rhi::ShaderPackage` type and binary schema are unchanged.
- User-directed layering is Renderer → Shader → RHI → Base, with no adapter or
  integration module. The guard now permits Renderer → Shader. Core Shader
  types/configuration/dependency records/cache/rebuild state link without
  glslang and return `unsupported` for source compilation; Renderer files were
  not changed in this track.
- Local Apple Silicon Ninja Debug / warnings-as-errors verification:
  `ctest --test-dir build/shader -R 'truffle_vulkan_tests|truffle_shader|shaderc'
  --output-on-failure` passes 8/8, including configured positive/negative CLI,
  actual installed static Shader consumers, existing package/assembly tests,
  and existing native Vulkan/MoltenVK graphics/compute/readback/presentation
  consuming shared-compiler `.tsp` outputs. No backend maturity is promoted.
  The compiler-disabled build's `truffle_shader|shaderc` selector passes 5/5,
  including installed core/Renderer/RHI consumers and explicit disabled errors.
  Both builds use the macOS SDK sysroot and `--parallel 4`.
- `python3 tools/check_architecture.py --report` passes. Compiler-disabled
  installed Shader/Renderer/RHI links have no glslang dependency. The
  compiler-enabled install bundles glslang exports and its static link closure;
  compiler-only runtime clients were also checked with shaderc disabled.
  No hosted work ran. Parent-owned global docs need the shared option/module
  description; the old flag in the Vulkan README's example also needs the
  shared option. #181 still owns Renderer publication and native runtime reload.

## Shader Library (#178)

- Added independently linkable `truffle_shader` / `Truffle::Shader`, public
  `truffle::shader` request/result, synchronous source resolver, cache and
  rebuild API. Allowed dependencies are RHI/Base and optional pinned glslang;
  no Renderer/backend/tool/EnCom dependency. See `src/shader/README.md`.
- `TRUFFLE_SHADER_ENABLE_GLSLANG=ON` works with shaderc disabled. The initial
  real route is GLSL/GLSL ES to Vulkan 1.1 SPIR-V 1.3, using glslang's own
  preprocessor for nested includes/defines. Results own an in-memory TSP,
  genuine dependency SHA-256 values, compiler identity, diagnostics and
  supported reflection. Cache keys cover effective configuration, target,
  compiler revision and transitive source contents; failures remain errors,
  with last-good state separately retained.
- Historical #181 baseline, superseded by #186 above: reflection covered declared uniform/storage buffers (including runtime-sized
  tails), separate images/samplers and fixed image/sampler arrays, scalar/vector
  IO and literal workgroups. Combined samplers, push constants,
  specialization and complex IO initially failed explicitly. That snapshot's
  TSP retained only pipeline-edge IO; #186 replaces this restricted layout.
- Verified warning-as-error Apple Silicon Ninja Debug build and focused
  compiler/package/existing-tool tests (5/5); compiler-disabled compiler/package/tool tests
  (4/4); real installed static consumers with compiler on and off. Enabled
  installs bundle glslang's export/link closure; prepared Renderer/RHI link
  lines contain only Renderer/RHI/Base. Architecture guard passes.
- Compiler instances are single-thread-owned; independent instances are tested
  concurrently, with one serialized glslang process lifetime. No file watcher
  or GPU hot-reload publication is owned here. Native Vulkan/Renderer adoption
  remains the separately tracked integration slice (#181), not a new maturity
  claim. #180 now makes shaderc a thin shared-library client and completes
  TSP naming (above); broader compiler routes remain #73 followups.

## Renderer Prepared Programs (#179)

- Renderer-owned TSP selection, shaders and eager native pipeline staging are
  implemented in `PipelineCache`; atomic publication preserves the existing
  material on failure. Exact configuration slots replace lossy layout hashes;
  binding/layout and attachment contracts cannot silently change on replacement.
- `IPipelineCache` returns typed pipeline results; low-level registration moves
  shader ownership. Batches support explicit RHI descriptor groups separately
  from vertex input. See `docs/renderer.md` for the public contract and #181
  integration seam.
- Ownership/cohesion: Renderer depends only on RHI/Base. Pipeline-cache program
  lifecycle is separated from frame recording in `pipeline_cache.cpp`. No shader
  source/compiler, host-window or higher-layer responsibility was added.
- Native evidence: hand-authored MSL packaged/serialized/loaded as TSPs drives
  real `RenderBatch` + `FrameGraph` execution on Metal. All 256 RGBA8 pixels
  match red (initial/reuse/staged), green (published replacement and retained
  output after failures), black (changed write mask), and blue (uniform group).
  Recording an old pipeline before replacing its owners still submits red.
  Intentional native shader and pipeline failures preserve native status/detail
  and working output; incompatible reflection/layouts/attachments are rejected.
- PR #182 review follow-up separates layout-mismatch coverage from reflected
  binding mismatch. Null and Metal now stage candidates with the same exact
  pipeline layout, attachment state and binding count, but a reflected minimum
  buffer size of 4 instead of the published 16 bytes. Both candidates stage
  successfully, commit returns `invalid_argument`, and the published pipeline
  ID stays unchanged. Metal still reads exact blue across all 256 pixels.
  The two affected program/native tests pass with Metal API/GPU validation;
  `git diff --check` passes. No production contract or code changed.
- Verified: Ninja Debug with macOS SDK, warnings as errors, compiler tooling
  disabled; 12 focused tests pass (Renderer/program/native Metal, frame graph,
  EnCom adapter, transform, existing Metal and three host smoke tests).
  Metal API/GPU validation was enabled with all three required environment
  variables. Architecture guard and `git diff --check` pass. The native test
  requires actual availability on Apple hosts rather than succeeding on a skip.
- Added `examples/prepared_renderer`: a small no-window file-loaded TSP consumer
  with enabled Metal/Vulkan selection. Its Metal execution succeeds after
  destroying caller packages. Native Vulkan/GL Renderer evidence remains future
  work; the reusable readback harness is backend-neutral.
- Delivery uses signed/pushed #179 commits on PR #182; parent reviews/merges. #181 can
  feed compiled TSP results into `ProgramDesc`, then `stage_program` and
  `commit_program`. Per the user decision, #181 adds direct Renderer → Shader
  use after #178 lands, not an intermediate adapter module. #179 adds no Shader
  or source compiler dependency. Dynamic-state and
  push-constant batches remain explicitly outside this first slice. Backend
  maturity is unchanged.

## Current Focus

The Apple Silicon macOS baseline is complete: Metal conformance audit (#69,
PR #174), Vulkan via MoltenVK (#56, PR #170), and OpenGL 4.1 host-view
presentation, fences and occlusion queries (#61/#63 macOS parts, PR #176);
combined develop build passes 37/37 ctests. Open follow-ups: Metal queries
(#175), MoltenVK packaging/iOS (#172), Linux/Windows parts of #61/#63, and the
remaining shader-compiler routes (#73).
Current work is parallel shared Shader library/tooling (#178/#180) and native
prepared-TSP Renderer execution (#179), followed by optional runtime
compilation and safe rebuild adoption (#181). Both tracks use native Git
worktrees. Prepared-package rendering does not require source compilers.
Other backends/platforms,
Phase 6 advanced features, and Phase 7 hardening are backlog listed in
`docs/roadmap.md`; their issues stay open.
Verification is local; other platforms go on the roadmap's pending
verification list instead of blocking development.
Issue #166 records this planning update; #165 tracks future self-hosted runner
setup, not immediate work. No issue labels or hosted workflows change.

## Shared Shader And Renderer Direction (#178-#181)

- Agreed naming: Truffle Shader Package (TSP), `.tsp`, retaining the C++
  type `truffle::rhi::ShaderPackage`.
- Shader owns source composition/configuration, compilation, reflection,
  dependencies, compilation caching, and rebuild mechanisms. Tools and runtime
  clients share that implementation; no new shader language is planned.
- Renderer owns GPU shader/pipeline resources, binding compatibility,
  pipeline caching, and validated replacement. RHI/backends remain independent
  of Shader, Renderer, and compiler toolchains.
- Renderer depends on Shader directly; there is no EnComRenderer-style
  adapter module. Compiler toolchains are optional Shader build parts, so
  Renderer stays compiler-free when they are disabled.
- Two contracts: prepared TSP results, plus optional source compilation/rebuild
  requests and results. An in-memory package is sufficient; no disk roundtrip
  is required.
- Delivery is signed, issue-scoped commits with immediate pushes and
  merge-commit PRs. Shader library/tooling and Renderer work are independently
  implemented and validated; end-to-end integration follows both.
- Architecture documentation records accepted direction. Native implementation
  evidence for the new tracks is not yet claimed; #73 remains broader than the
  initial GLSL route.

## Apple Silicon Scope And #56 Split (#171, #172)

- Decision (#171): macOS support targets Apple Silicon only. Intel Macs are
  not a target; Intel-compatible code (Metal managed-storage flush, eGPU
  removal mapping) stays as harmless code. Intel items are removed from the
  roadmap pending-verification list, READMEs and matrix notes;
  PROJECT_CONTEXT records the decision.
- #56's macOS lane is delivered (PR #170); its packaging, package isolation,
  iOS-family lanes and MoltenVK timestamp re-check move to deferred #172
  (sub-issue of #33), so #56 closes.
- Next: review/merge the macOS GL presentation and sync PR (#61, #63, branch
  `feat/opengl-macos-present-sync`); Metal advertised-capability conformance
  audit (#69).

## OpenGL macOS Sync, Queries And Context Ownership (#63)

- Same branch as #61. `gl_sync.hpp` owns completion, semaphores and query
  pools; `TRUFFLE_GL_HAS_SYNC_OBJECTS` is defined for CGL only, so Linux EGL
  keeps semaphores/query pools `unsupported` until verified there.
- Completion: submit and present end with `glFenceSync` +
  `glClientWaitSync` (flush bit, 10 s bound); an unsignalled fence returns
  `device_lost`, a wait failure `backend_validation_failed`. Without sync
  objects it falls back to `glFinish`. RHI fences/semaphores stay host
  timeline values reached at submit.
- Queries: occlusion pools use `GL_SAMPLES_PASSED` and resolve exact uint64
  counts with `glBufferSubData`; resolving an unended query is
  `invalid_state`. Timestamp pools are `unsupported` (Apple reports zero
  `GL_TIMESTAMP` counter bits; `GL_TIME_ELAPSED` exists but has no RHI query
  type) and pipeline statistics are `unsupported` (GL 4.6).
  `timestamp_queries` is not advertised; `timestampPeriodNanoseconds` is 0.
- Context ownership: one CGL context and graphics queue per instance; every
  GL call holds the context mutex and makes it current on the calling thread,
  so any thread may record/submit but GL work is serialized. Presentation is
  AppKit-main-thread only.
- Evidence: `verify_gl_sync` in `truffle_opengl_tests` (fence 7/8 timeout,
  semaphore 1 ns timeout leaves the list executable then retries, occlusion
  counts 256/16/0 with an untouched leading word, unsupported pool types,
  unended-query rejection, four threads x 24 submits with exact readback,
  zero GL errors). Both GL ctests pass 10 repeated runs.
- Not done: EGL/GLES sync objects and queries (verify on Linux), WGL.

## OpenGL macOS Host-View Presentation (#61)

- Branch `feat/opengl-macos-present-sync` (from develop `c6779c8`). New
  public `NativeSurfaceKind::cocoa_view` (a host-owned `NSView*`, appended to
  the enum). The host owns window, event loop and view settings; Truffle
  never creates windows.
- Backend: `cgl_view.{hpp,mm}` is the only AppKit code (manual
  retain/release, AppKit NSOpenGL deprecations silenced in that file only).
  `gl_present.hpp` implements surface/swapchain/acquire/resize/present
  (host-value semaphores moved to `gl_sync.hpp` in #63). The swapchain owns one
  `NSOpenGLContext` sharing objects with the device CGL context and one
  back-buffer texture; present blits it with a Y flip (texture row 0 = window
  top), checks `glGetError`, `flushBuffer`s and waits for completion (a GL
  fence since #63), then restores
  the device context. `bgra8_unorm` was added to the desktop GL format table
  (the SwapchainDesc default).
- Contract: RGBA8/BGRA8; `fifo`/`immediate` (swap interval 1/0), `mailbox`
  `unsupported`; drawable drift gives `suboptimal` (present scales), a view
  without a window gives `out_of_date`; all presentation calls require the
  AppKit main thread (`invalid_state` otherwise); wrong surface kind
  `unsupported`, non-NSView handle `invalid_argument`.
- `Feature::presentation` and `AdapterInfo::presentation` are now advertised
  on macOS GL; the `backend_support.cpp` macOS GL evidence gains
  `presentation`.
- host_workspace: `MetalHostPresenter` became backend-neutral `HostPresenter`
  (takes an instance, native surface and format; counts suboptimal
  presents), the run loop is shared, and `--opengl [--frames N]` passes the
  GLFW content view (`host_window_cocoa_view.mm`). New ctest
  `truffle_host_workspace_opengl_window` (120 frames, one scripted resize).
- Evidence (Apple M3 Pro, macOS 27.0.1, Xcode SDK 27, Debug, Metal+OpenGL
  on): all 32 ctests pass, including new `truffle_opengl_presentation_tests`
  (back-buffer pixel checks, resize, `out_of_date`, negatives) and both host
  window runs; repeated GL window runs report
  `frames=120 resizes=1 extent=1280x800->1600x1040`. Architecture guard
  passes. `clang-format` was not installed, so the format check did not run.
- Not done: Linux EGL window surfaces and WGL presentation (pending
  verification on those machines).

## OpenGL macOS Offscreen CGL (#61, #63)

- Branch `feat/opengl-macos`. The shared GL command code is renamed
  `gl_probe.hpp`/`gl_graphics.hpp`/`gl_program.hpp` (namespace
  `detail::gl_probe`); context creation, make-current and proc loading move
  behind a private seam: `egl_context.hpp` (Linux, unchanged behavior) and
  `cgl_context.hpp` (macOS). `gl_api.hpp` picks headers and feature macros
  (`TRUFFLE_GL_HAS_COMPUTE`, `_TEXTURE_STORAGE`, `_TEXTURE_VIEWS`,
  `_BASE_INSTANCE`, `_DEBUG_OUTPUT`); CGL defines none of them.
- macOS creates an accelerated `kCGLOGLPVersion_GL4_Core` context (GL 4.1),
  `GL_SILENCE_DEPRECATION` only on `truffle_backend_opengl`. Textures without
  immutable storage allocate each level/face with `glTexImage*`. Nonzero base
  instance (direct, or the indirect first-instance word) returns
  `unsupported`. The adapter smoke now clears an FBO on both contexts.
- Advertised on macOS: transfer, descriptor arrays, dynamic offsets, push
  constants; identity views, copies, fills, clears, resolve, blits;
  graphics/MRT/depth-stencil/MSAA/indirect. Not advertised: compute/storage
  blocks, subresource views, KHR_debug, external resources, presentation.
- Evidence (Apple M3 Pro, macOS 27.0.1, adapter `Apple M3 Pro (4.1 Metal -
  91.7)`): `truffle_opengl_tests` runs the shared buffer, texture, direct and
  expanded graphics, binding and compute-negative suites with GLSL 410 and
  zero GL errors; all 25 ctests pass with Metal and OpenGL on. Validation is
  per-call `glGetError` checking with zero errors required; no
  debug-message layer exists on macOS GL 4.1 (user-approved wording in the
  matrix, support note, README and PR). The expanded graphics suite asserts
  nonzero base instance (direct, indexed, indirect) submits `unsupported` on
  macOS and the context still renders afterwards.
- Architecture guard: 19 dependency declarations (+1 for the private
  `OpenGL.framework` link).
- Still open: WGL/EGL window presentation (#61; macOS host-view
  presentation is done above), EGL/GLES sync objects and queries (#63;
  macOS is done above), Windows verification (roadmap pending list). Linux EGL is compile-safe by
  inspection only on this run.

## Metal macOS Host Window (#69)

- Branch `feat/metal-macos-host`. The macOS 27 SDK deprecates
  `MTLCommandBufferErrorDeviceRemoved`, `MTLStorageModeManaged`, and
  `didModifyRange:` (no-ops/unreachable on Apple Silicon), which broke the
  warnings-as-errors build. The backend keeps both paths (harmless Intel-compatible
  code) and suppresses the warning only around those uses.
- Verified locally on Apple M3 Pro, macOS 27.0.1: `truffle_metal_tests` and
  `truffle_metal_presentation_tests` pass with `MTL_DEBUG_LAYER=1
  MTL_SHADER_VALIDATION=1`.
- `examples/host_workspace --metal` now opens a host-owned GLFW window and
  passes only its `CAMetalLayer` to Truffle as a `cocoa_layer` surface.
  `MetalHostPresenter` acquires, clears via `begin_rendering`, presents, and
  resizes the swapchain on framebuffer change, `suboptimal`, or `out_of_date`.
  `--metal --frames N` scripts one `glfwSetWindowSize` halfway and exits
  non-zero unless all frames present and the swapchain tracks the new
  framebuffer. ctest `truffle_host_workspace_metal_window` (labels
  `presentation;host-window;validation`) runs 120 frames and needs a logged-in
  GUI session. The Renderer is unchanged; the old one-submit `--metal`
  foundation path is replaced.
- `truffle_metal_tests` now also proves advertised indirect support beyond
  the MRT draw: an indirect dispatch reproduces the direct compute output, an
  indexed indirect draw alone covers a cleared target, and timestamp query
  pools (not advertised) return `unsupported`.
- Evidence (2026-10-29, Apple M3 Pro, macOS 27.0.1, Xcode 27.1): with
  `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 METAL_DEVICE_WRAPPER_TYPE=1`,
  nine Metal-linked ctests pass (metal, presentation, host-window, backend
  support, RHI contract, parity, three Null host smokes) with no validation
  messages; the host-window run is repeatable (five consecutive runs). The
  Metal README records the commands and hardware table.
- Physical device removal cannot occur on Apple Silicon; device-loss
  evidence stays private fault injection and is documented as such. Intel Macs
  are not a target (#171). No capability flag or
  `backend_support.cpp` row changed: Metal stays `native_smoke`.
- Advertised-feature conformance followed in the next section.

## Metal Advertised-Capability Conformance (#69)

- Branch `feat/metal-conformance-audit`. Every advertised Metal capability,
  format and limit now maps to a hardware test; the table lives in
  `src/backends/metal/README.md`. New `truffle_metal_conformance_tests`
  (labels `native-smoke;validation`) fills the gaps with readback: all eight
  color and three depth formats, mips, stencil, blend, 16 vertex buffers,
  16 viewports/scissors, eight color attachments, 256-byte push constants,
  dynamic offsets, 2x8 bind groups, workgroup limits, mapping, sRGB view
  reinterpretation, and rejection just past each limit.
- The backend now implements texture clear (whole subresource, color and
  depth/stencil aspects), multisample resolve (whole color subresources) and
  nearest/linear color blits (a Truffle-owned fullscreen draw with
  runtime-compiled MSL); partial clears/resolves and depth or multisample
  blits return `unsupported`. `depth32f_stencil8` buffer-texture copies now
  move one aspect via Metal blit options; combined-aspect copies and host
  access return `unsupported` instead of failing validation.
- Timestamp queries stay unadvertised: on the M3 Pro, counter sampling exists
  only at encoder boundaries, empty encoders record no sample, and GPU-side
  `resolveCounters` returned zeros. Occlusion queries remain unmapped. Both
  are tracked in #175.
- `backend_support.cpp` records Metal macOS conformance evidence; maturity
  stays `native_smoke` because the `conformant` gate also needs the shared
  cross-backend suite, which Metal (MSL shaders) does not run yet.
- Evidence (2026-10-30, Apple M3 Pro, macOS 27.0.1, Xcode 27.1): all 30
  ctests pass with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1
  METAL_DEVICE_WRAPPER_TYPE=1` and no validation messages.

## Vulkan macOS MoltenVK Lane (#56)

- Branch `feat/vulkan-macos`. The Vulkan test binary previously did nothing
  off Linux. On macOS it now runs the shared suite plus a `CAMetalLayer`
  presentation proof; adapter platform/maturity are checked against
  `host_platform()` and the support table.
- Backend: `VK_EXT_metal_surface` behind `VK_USE_PLATFORM_METAL_EXT` (set on
  Darwin) accepts only `cocoa_layer`; WSI gates use `TRUFFLE_VULKAN_HAS_WSI`
  (XCB or Metal). After volk's default search fails, Apple builds try the
  Homebrew/`/usr/local` `libvulkan.1.dylib`. Injected `suboptimal`/
  `out_of_date` present faults now present natively before returning, which
  fixed an infinite acquire wait on MoltenVK's small swapchain.
- Evidence (2026-10-29, Apple M3 Pro, macOS 27.0.1, Homebrew MoltenVK 1.4.2,
  loader 1.4.363.0): all 26 ctests pass, including `truffle_vulkan_tests`
  (the loader itself needs no `DYLD_*` variable). `D24_UNORM_S8_UINT` is unsupported on Apple
  GPUs and returns `unsupported`; the test falls back to `D32_SFLOAT_S8_UINT`.
- Validation (2026-10-30): with Homebrew `vulkan-validationlayers` 1.4.363.0
  the suite reports zero validation messages, also with
  `VK_LAYER_VALIDATE_SYNC=1`. The macOS row is now `native_smoke` (compile,
  smoke, validation, presentation). The macOS ctest sets
  `DYLD_FALLBACK_LIBRARY_PATH` to the found loader directory, because the
  Homebrew layer manifest uses a bare dylib name. It fails on
  `Validation (Error|Warning)` output. Before this, an enumerated layer that
  could not load made instance creation fail, so default
  (`enableValidation = true`) apps and `truffle-rhi-doctor --strict` lost
  Vulkan entirely. The backend now retries without the layer on
  `VK_ERROR_LAYER_NOT_PRESENT`, and the macOS test fails on that state via a
  private test hook.
- Timestamps (2026-10-30): after the rebase onto 7ffbb4e the suite failed
  about 3-10% of runs because MoltenVK 1.4.2 resolved the second timestamp
  to 0 when the copy ran in the same submission (an extra barrier did not
  help; a separate submission did). Validation reported nothing. The backend
  now reads the driver ID through `VK_KHR_driver_properties` (the 1.1
  instance clamps `apiVersion`) and does not advertise timestamp queries on
  MoltenVK; the test derives timestamp checks from advertised features and
  asserts `unsupported` otherwise. 60 consecutive runs and three full
  29-test ctest runs passed.
- #56 is complete for macOS; packaging, isolation and iOS-family lanes are
  deferred to #172.

## Module Taxonomy Update (#161)

- Docs record the agreed module map: Engine (planned) → Renderer → RHI →
  Base; EnCom → Base; optional EnComRenderer → EnCom + Renderer; GUI
  (planned) → Renderer without EnCom; Assets deferred with name/design
  undecided; Simple RHI and a windowing convenience deferred; shaderc and
  rhi-doctor are developer tools, not modules.
- Current names: `truffle_renderer`, `truffle_encom`, `truffle_encom_renderer`
  with `EnComRenderAdapter`/`EnComRenderFrame`. No aliases or forwarding
  headers exist.

## Compatibility Scaffolding Removal (#163)

- `truffle/base/version.hpp` keeps only version identity (`ApiVersion`,
  `kApiVersion`, packing). ADR 0010 is amended.
- No version, dependency direction, or runtime behavior changed.

## Delivery Workflow Update

- AI-assisted implementation defaults to review-first delivery. Before an AI
  implements a plan, the user selects review-first or commit-and-push delivery
  and agrees the proposed cohesive or staged commit structure.
- Every AI-authored commit is pushed to the same branch's configured upstream
  immediately. A failed push leaves delivery incomplete; no later commit or
  branch switch occurs before the push is recovered through an approved access
  path.
- Protected branches remain pull-request-only. The repository-local agent and
  contributor guidance record the workflow; no RHI contract or backend
  capability changed in this update.
- Git Flow retains `master`, `develop`, feature/fix, release, and hotfix
  branches. Normal merge commits preserve history; squash/rebase requires an
  explicit, justified exception.
- Focused local checks are the default. Build and Release are explicit manual
  opt-ins, not PR/push/tag gates. Automatic companions and their credentials
  are not part of the contribution workflow.

## Architecture Guardrail Update

- `tools/check_architecture.py` checks production CMake target dependencies
  and public-header imports against Truffle's documented layer direction.
  Run it locally for relevant changes; the optional manual Build includes it.
- The guard reports files at or above the 1,000-line review threshold without
  failing or calling them God files. Every changed production file still gets a
  cohesion review based on owner, lifecycle, and reason to change; small,
  mixed-responsibility files are candidates too.
- `docs/architecture-review.md` defines the required plan/review note and the
  orchestrator boundary. The guard passed locally for 16 production targets,
  18 direct target dependency declarations, and 76 public-header imports.

## Latest Handoff

- #159 renames the shared Core module to Truffle Base consistently:
  `truffle_base`, `Truffle::Base`, `truffle::base`, and `truffle/base/` headers.
  Source/header paths, consumers, contract-test naming, installed exports,
  architecture ownership rules, and current docs use Base. The README records
  old-to-new spellings and advises a fresh install prefix. There are no legacy
  aliases, forwarding headers, or root-namespace flattening.
- Base retains the existing status/result, typed-handle, configuration, and
  version contracts as a dependency-free leaf. All 78 production files match
  the exact naming-only transformation from the starting revision. Dependency
  direction and behavior are unchanged; package and API versions remain
  `0.1.0` because this is unreleased implementation work, not a release boundary.
  No architectural boundary or backend capability/maturity changes.
- Focused local Debug builds with warnings-as-errors pass Base, API-version,
  ECS, Null RHI, diagnostics, and scene-adapter contracts plus the installed
  package consumer: seven selected tests pass. The installed consumer also
  executes successfully using all four Base headers and the exported
  `Truffle::Base` target. The fresh install contains the Base library/headers
  and no former Core library/header directory. The architecture guard preserves
  16 production targets, 18 dependency declarations, and 76 public-header
  imports; diff checks pass. No full platform matrix or hosted workflow is
  needed for this naming-only change.
- #131 is implemented as an intentional replacement of cloud-companion repair:
  the companion/setup workflows, router, and now-unused routing tests are
  removed. Build and Release have only manual dispatch triggers; Release
  explicitly rejects branch refs and non-`v*` tags before packaging.
  Contributor, agent, distribution, and project-context guidance now describe
  local-first validation, Git Flow release/hotfix propagation, and merge commits.
  There is no architectural boundary or public-contract change.
- All four repository-defined workflows were administratively disabled before
  publishing the policy branch, covering legacy automatic triggers on `master`
  without promoting engine changes there. The companion secret remains absent.
  GitHub's built-in Copilot reviewer registration cannot be disabled through the
  Actions workflow API; there is no repository automatic-review ruleset.
  Signed commits, PR-only protection, admin enforcement, and no-force-push/
  no-deletion protections remain; no required hosted checks were introduced.
- Lightweight local verification confirms both workflows are manual-only and
  preserves the native/platform/package job bodies. Release-ref checks accept a
  `v1.2.3` tag and explicitly reject a normal branch, a `v1.2.3` branch, and a
  non-release tag. The architecture guard passes for 16 production targets,
  18 dependency declarations, and 76 public-header imports; `git diff --check`
  passes. No C++ rebuild or hosted workflow was requested for this policy change.
- #62's full GL-family checkpoint passes Build `40230852839` at `d8c885d`:
  Ubuntu native EGL, macOS, Windows, architecture and package jobs are green.
  Both profiles prove exact indexed/instanced, depth/stencil, MRT, dynamic-state
  and textured output, including logical remaps, sampled arrays, dynamic
  offsets, immutable samplers, stage push blocks, scalar specialization and
  arena retention/reset. Desktop GL additionally proves indirect drawing,
  MSAA resolve and direct/indirect compute-to-render. Tests reject reflected
  name/type/size, missing-resource and remap mismatches, and require zero
  KHR_debug diagnostics. Simultaneous ordinary/shadow sampling through one RHI
  sampler uses separate native comparison objects, not mutable shared state.
  Native-source ShaderPackage fixtures retain explicit assembler provenance.
  GLES 3.0 keeps storage blocks/images, compute, indirect/base-vertex/base-
  instance drawing, MSAA and subresource views unsupported; identity sampling
  views are supported. Both profiles remain `native_smoke`, not conformant.
- `gl_program.hpp` owns program interface policy and `gl_graphics.hpp` owns
  graphics state/input details. The GL command interpreter remains cohesive
  around submission lifecycle and native-state ownership; dependencies and
  public RHI contracts are unchanged. The support matrix, shader conventions,
  resource boundaries and roadmap are synchronized for PR #156 closeout.
  The approved branch cleanup retains `develop`/`master`, preserves all three
  working folders/build assets, saves a verified Git bundle first, and removes
  only reviewed integrated/superseded branch refs plus one stale registration.
- PR #139 merged at `6b663bf` after Build `40229333169` passed its architecture
  guard, all platform jobs and package isolation. PR #157 merged at `07b19da`
  after all Build lanes and nine automation regressions passed. Its diagnostic
  implementation remains recoverable in history; the later #131 decision
  deliberately retires that integration instead of pursuing cloud dispatch.
- `feat/rhi1-gl-pipelines` starts #62 with the native-source and direct-graphics
  checkpoint for both Linux EGL profiles. Native GLSL/GLSL ES ShaderPackage
  variants compile to owned shader objects with bounded source, `main` entry,
  stage, compile-log, and link-log validation. Linked program objects retain
  shader lifetime through the common RHI payloads. The shared queue path now
  owns framebuffer and vertex-array state across a dynamic-rendering scope,
  performs attachment clears, binds a program, and executes direct non-
  instanced draws before returning to the existing exact transfer/readback
  path. Both profiles conservatively advertise one color attachment, no vertex
  streams, one viewport, and no compute/depth/MRT/MSAA/indirect/tessellation
  capability yet. Shared tests build contributor-neutral native-source fixture
  packages with explicit assembler provenance, compile/link a vertex-ID
  triangle, render an exact red center pixel, and require zero KHR_debug
  callbacks independently for desktop GL and GLES. Ownership remains in the
  shared GL-family backend, system EGL/GL are the only native dependencies, and
  the public contract is unchanged. Local cross-platform compilation passes.
  Initial Build `33965142135` reached Linux and caught that the configured GLES
  3.0 headers omit the compute-stage token. Because compute remains deliberately
  unadvertised in this checkpoint, the GLES stage mapper now rejects it without
  referencing a later-profile declaration. Replacement Build `33965233739`
  passes package, macOS, Ubuntu, and Windows. The Ubuntu lane proves exact red
  triangle pixels and zero KHR_debug callbacks independently on desktop GL and
  GLES. The support matrix, pipeline table, README, architecture, overview, and
  project context record this deliberately narrow checkpoint; #62 remains open
  for the advanced binding and pipeline acceptance surface.
- `feat/rhi1-gl-extended-resources` continues #60 from the merged 2D transfer
  checkpoint. The shared EGL implementation now selects immutable targets for
  2D arrays, mip chains, 3D textures, cube/cube-array textures, and desktop GL
  multisample textures; both profiles own core depth16 and depth24/stencil8,
  while desktop GL additionally owns its core 32-bit depth variants. Uploads
  honor row and image pitch, and framebuffer attachment selects the requested
  array layer, volume slice, cube face, and color/depth/stencil aspect. Desktop
  GL exposes runtime-gated core texture views and multisample resolves; GLES
  continues to report both explicitly unsupported. Validation-enabled contexts
  install KHR_debug when the driver exposes it and route API errors through the
  existing instance callback. Shared native tests exercise separate profile
  capability snapshots, exact mip/array/3D/cube/depth paths, desktop resolve,
  GLES negative MSAA/view behavior, external boundaries, memory budgets, and a
  zero-error debug receipt. Ownership remains in the shared GL-family backend,
  dependencies remain system EGL/GL only, and the public RHI contract is
  unchanged. Local cross-platform compilation and the existing 37-test suite
  pass. Initial Build `33963590121` reached the Linux GLES translation unit and
  caught two profile-header assumptions: Ubuntu's GLES 3 headers omit KHR_debug
  typedefs/constants and `GL_PACK_IMAGE_HEIGHT`. The shared code now uses the
  specification values with a local callback signature, and skips the pack-
  image state that GLES does not expose because readback is deliberately one
  slice at a time. Replacement Build `33963718837` passes all four lanes; its
  Ubuntu lane compiles both profiles and passes the extended exact native suite
  with KHR_debug enabled. A final packed depth/stencil assertion now clears both
  aspects and reads stencil independently, while direct multisample transfer
  attempts return unsupported and require the advertised resolve path. Build
  `33964084454` caught that Ubuntu's GLES headers also omit the core numeric
  `GL_STENCIL_INDEX` token even though the transfer path can name it. The shared
  profile constants now supply that specification value locally. Final Build
  `33964186004` passes package, macOS, Ubuntu, and Windows. The Ubuntu log proves
  both GL-family native suites, including independent packed-stencil readback,
  desktop multisample resolve, profile-specific negatives, and zero KHR_debug
  error callbacks. Support matrix, resources, roadmap, README, and project
  context now carry the same capability boundary. #60 is ready to close when
  this PR merges; #61 is the next P0 GL-family slice.
- `feat/rhi1-gl-resources` starts #60 with a shared 2D texture checkpoint for
  both Linux EGL profiles. The backend now owns immutable uncompressed color
  textures and sampler objects, rejects external and unsupported shapes
  explicitly, and implements host texture read/write plus buffer-to-texture,
  texture-to-buffer, texture copy, partial clear, and nearest/linear framebuffer
  blit through profile-common APIs. Every operation binds its own buffer,
  texture, framebuffer, and pixel-store state and restores transient pixel-
  store/framebuffer bindings before completion. The shared native test checks
  exact padded-row upload/copy/readback, exact clear pixels, linear-blit output,
  host upload/readback, sampler creation, state rebinding across resources, and
  negative view/3D/external paths independently on desktop GL and GLES. The
  public contract is unchanged; ownership remains in the shared EGL backend
  with only system EGL/GL dependencies. Initial Build `33962059122` compiled
  both profiles but exposed a capability wiring defect: the common sampler
  factory correctly existed, but the foundation rejected sampler creation
  before reaching it because ordinary binding objects were not advertised.
  Both profiles now expose the conservative logical bind-group baseline and
  bounded limits required to create sampler objects; the shared test asserts
  that prerequisite explicitly. Replacement Build `33962339263` then passed
  the sampler and every native texture operation but caught an incorrect test
  expectation at the final external-memory boundary: ordinary creation with
  the external memory domain is contractually invalid because imports must use
  `import_texture`, whereas a shareable ordinary allocation is the operation
  that reports unsupported on these profiles. The test now covers both results
  separately. Final Build `33962754752` passes package, macOS, Ubuntu, and
  Windows. Its Ubuntu native-smoke lane proves both desktop GL and GLES exact
  transfer/clear/blit/readback results and the explicit unsupported boundaries.
  Texture views, extended shapes/formats,
  depth/stencil, and multisample resolve remain inside open issue #60 for the
  next checkpoint.
- `feat/rhi1-vulkan-linux-wsi` implements the Linux Vulkan presentation path
  for #57 without adding a window framework. Hosts pass an owned XCB connection
  and window through the existing `NativeSurface`; the backend enables
  `VK_KHR_surface`, `VK_KHR_xcb_surface`, and `VK_KHR_swapchain` only when the
  loader and adapter expose them. Surface capability, exact format, FIFO mode,
  image-count, extent, usage, queue presentation support, acquire, present,
  and resize/recreation are native and fail explicitly when unsupported.
  Acquisition waits on a native fence before returning the already-complete
  RHI acquire timeline point. Presentation bridges RHI timeline waits into a
  WSI-required binary semaphore. Borrowed swapchain images retain native
  present layout tracking but never destroy swapchain-owned `VkImage` handles.
  A Linux-only XCB test creates its own tiny host window under `xvfb-run`,
  clears a BGRA swapchain image, copies it into padded-row readback, checks every
  pixel, presents it, resizes the host window, recreates the swapchain, and
  repeats the exact proof. Unsupported surface kinds are also checked. The
  internal surface callback now receives the already-existing native context;
  Metal and D3D12 are threaded through with no public API change. The local
  macOS build and all 37 configured tests pass. Build `33945857429` passes
  package, macOS, Ubuntu, and Windows. Its Ubuntu lane compiles the Linux-only
  path, runs the full native suite with Khronos validation, and then runs the
  presentation-labeled Vulkan test through `xvfb-run`; both presentation tests
  pass. Private, non-installed acquire/present fault hooks additionally verify
  that timeout, suboptimal, out-of-date, surface-loss, allocation, and device-
  loss outcomes cross the public boundary correctly; retryable presentation
  failures preserve the acquired image, while replacement statuses release it.
  Initial receipt Build `33946162926` exposed three real portability defects:
  native swapchain image counts can exceed the requested minimum, Vulkan 1.1
  instances must enable the promoted timeline extension, and never-presented
  swapchain images begin undefined. The foundation now accepts backend-native
  image indices, extension selection follows the requested instance version,
  and each image receives a one-time undefined-to-present transition after its
  first native acquire. The same log also exposed combined depth/stencil layout
  transitions using only one aspect without the separate-layout feature; such
  barriers now cover both aspects. Replacement Build `33961381232` passes
  package, macOS, Ubuntu, and Windows. The Ubuntu log contains no Vulkan
  validation errors; its native and Xvfb presentation suites pass, including
  all typed recovery cases. The support matrix and roadmap claim only the
  evidenced native XCB path.
- `fix/rhi1-vulkan-device-loss` closes the final #54 acceptance gap found after
  PR #150 merged. A private, non-installed Vulkan test hook injects submission
  device loss; the native suite proves the submitting device becomes lost,
  further work remains rejected after the hook is cleared, and a freshly
  requested device recovers. `AdapterInfo::timestampPeriodNanoseconds` now
  makes raw query ticks calibratable: Vulkan publishes native
  `timestampPeriod`, D3D12 publishes the inverse queue frequency, and Null uses
  one nanosecond per deterministic tick. Timestamp-capable adapter tests require
  a positive period. The public addition is backend-neutral and belongs to the
  RHI adapter contract; the fault hook remains Vulkan-private. The local build
  and all 37 tests pass. Build `33945096780` passes package, macOS, Ubuntu, and
  Windows; Ubuntu proves Vulkan loss/recovery under validation and Windows
  proves D3D12 timestamp-frequency compilation and runtime calibration. The
  separate companion-routing failure remains tracked by #131.
- `feat/rhi1-vulkan-sync-foundation` implements #54's native Vulkan baseline.
  Queue kind is now carried through the internal submission boundary; Vulkan
  discovers graphics, compute, and transfer families, creates one queue for
  each distinct family, and uses concurrent sharing for resources visible to
  multiple families. Explicit buffer, per-subresource texture, and aliasing
  barriers map portable stages, access masks, and layouts to Vulkan. Native
  timeline semaphores use Vulkan 1.2 or `VK_KHR_timeline_semaphore`; timestamp
  and occlusion pools reset, record, availability-wait, and resolve 64-bit
  results. The public fence remains the foundation's conservative completed-
  submission timeline because Vulkan submission waits for queue idle before
  returning. The validation-enabled native test proves transfer-to-graphics
  semaphore ordering, multi-list submission, logical ownership, exact buffer
  output, fence timeout/retry, explicit barrier classes, timestamp ordering,
  nonzero bounded occlusion, and deterministic unsupported pipeline statistics.
  The internal queue-kind parameter is threaded through the other native
  backends without changing the public API. Ownership stays in RHI foundation
  and `src/backends/vulkan`; dependencies remain the existing RHI contract and
  pinned Vulkan headers/volk. The local build and all 37 tests pass. Initial
  Build `33944445340` reached GCC and MinGW compilation and identified two
  warnings-as-errors: an enum/flag conditional needed an explicit Vulkan flag
  cast, and the Vulkan features wrapper needed an explicit zero initializer.
  Both portability fixes are applied. Replacement Build `33944555436` passes
  package, macOS, Ubuntu, and Windows; the Ubuntu validation lane proves the
  native cross-queue, barrier, timeline, fence, and query paths. The separate
  companion-routing failure remains the non-gating defect tracked by #131.
- `feat/rhi1-vulkan-graphics-closeout` completes #53's remaining acceptance
  surface. A generated fragment package reads the storage buffer written by a
  preceding compute dispatch in the same command list and produces an exact
  blue triangle; submission inserts compute-write to fragment-read visibility
  before beginning the render pass. Native tests also prove that recorded bind
  groups retain their resources across an arena reset, retired groups fail
  deterministically, and replacement groups submit exact output. Missing and
  wrong-type reflection layouts, non-identity SPIR-V remaps, update-after-bind,
  bindless tables, pipeline caches, and indirect-count all return their
  documented invalid/unsupported status without simulation. Pipeline-cache and
  bindless execution remain untested because they remain unadvertised, as #53
  requires. Ownership stays in `src/backends/vulkan`, dependencies remain RHI
  contracts plus pinned Vulkan/glslang inputs, and there is no public API
  change. The local pinned-glslang build and all 37 tests pass. Replacement
  Build `33943018863` passes package, macOS, Ubuntu, and Windows; Ubuntu Vulkan
  validation proves exact compute-to-render output, arena lifetime behavior,
  and every selected negative gate. This satisfies #53's acceptance and the PR
  closes it on merge.
  Initial Build `33942881518` reached the native lifetime case and confirmed
  the retired group is rejected by the public encoder's stale-object guard as
  `invalid_argument`, before the lower-level arena epoch check; the assertion
  now matches that documented API boundary.
- `feat/rhi1-vulkan-depth-mrt-msaa` generalizes Vulkan graphics-pipeline and
  submission render-pass compatibility from one single-sample color target to
  multiple color targets, an optional depth/stencil attachment, and explicit
  multisample resolves. Native pipeline state now maps depth comparison/write
  and stencil faces, while unsupported depth clamp, non-fill rasterization,
  tessellation, and indirect-count remain deterministic failures. A generated
  two-output fragment package drives exact red/green MRT readback; depth-clear
  cases prove pass/fail output, and a four-sample target resolves to an exact
  red pixel. Capabilities are promoted from hardware limits only with these
  paths present. Ownership remains in `src/backends/vulkan`, dependencies stay
  limited to RHI contracts and pinned Vulkan/glslang inputs, and the public API
  is unchanged. Both local warning-enabled builds pass; all 36 normal tests and
  all 37 pinned-glslang tests pass. Initial Build `33942096908` reached the
  native suite and exposed a stale single-target assertion plus a sampled-image
  layout barrier recorded inside an active render pass. Capability assertions
  now match the promoted limits, and descriptor images are transitioned at the
  render/compute encoder boundary before native pass execution. Ubuntu Vulkan
  validation in replacement Build `33942299645` proves exact depth pass/fail,
  two-target red/green output, and four-sample resolve with clean validation;
  package, macOS, Ubuntu, and Windows lanes all pass. The separate
  companion-routing failure is the known non-gating automation defect tracked
  by #131.
- `feat/rhi1-vulkan-graphics-expanded` extends #53's proven ordinary graphics
  slice with a generated sampled-texture fragment package, reflected texture
  and sampler descriptors, graphics bind-group encoding, direct indexed and
  indirect draw commands, and dispatch-indirect. The Linux-native suite uploads
  an exact green texel, samples it into the triangle target, verifies indexed
  and indirect red triangles, and reruns the storage-buffer compute fixture via
  indirect dimensions. Vulkan advertises indirect commands only after this
  native gate; indirect-count remains unsupported. Ownership and dependencies
  remain backend-local with no public API effect. The local warnings-as-errors
  build and all 36 configured host tests pass. Initial Build `33940792618`
  reached Ubuntu compilation and exposed test calls that omitted required
  public indirect-command arguments; the tests now state the exact offset,
  indexed mode, draw count, stride, and dispatch offset. Ubuntu Vulkan
  validation in replacement Build `33941005375` proves the exact texture,
  indexed/instanced/indirect draw, and indirect-compute results; package,
  macOS, Ubuntu, and Windows lanes all pass. The separate companion-routing
  failure is the known non-gating automation defect tracked by #131.
- `feat/rhi1-vulkan-graphics` implements the first bounded Vulkan graphics
  checkpoint for #53. Generated vertex and fragment SPIR-V packages create an
  owned native graphics pipeline and compatible render pass. Submission creates
  transient attachment views/framebuffers, records clear and direct draw
  commands, transitions the render target for copy, and reads back the center
  pixel exactly. The backend advertises one single-sample color target and
  direct graphics only; depth/stencil, MRT, MSAA, indirect commands, pipeline
  caches, and non-identity remaps remain false or unsupported. Ownership stays
  in `src/backends/vulkan`; it consumes only RHI contracts and the existing
  pinned Vulkan/glslang dependencies, with no public API change. The local
  warnings-as-errors build and all 36 host tests pass; Linux Vulkan validation
  is the native evidence gate before documentation or capability claims merge.
  Initial Build `33940133323` reached GCC and found enum-versus-flags
  conditional expressions rejected by `-Werror`; stage/access mask assembly
  now uses explicitly typed Vulkan flag variables. Replacement Build
  `33940219142` passes package, macOS, Ubuntu, and Windows; Ubuntu proves the
  exact red center pixel under Vulkan validation.
- `feat/rhi1-vulkan-bindings` adds the next bounded #53 checkpoint. Vulkan now
  creates native samplers, descriptor-set/pipeline layouts, compute pipelines,
  per-submission descriptor pools/sets, push constants, and direct dispatch on
  its shared graphics/compute queue. The generated GLSL ES fixture writes four
  deterministic words through a reflected storage-buffer bind group; the Linux
  lane must prove exact host readback under validation. Adapter limits drive
  the reported binding/push-constant/workgroup limits. Non-identity SPIR-V
  binding remaps, graphics pipelines, render passes, and indirect dispatch stay
  explicitly unsupported. The backend owns all native objects and consumes
  only existing RHI contracts plus pinned Vulkan dependencies; no public API
  changes. Local warning-as-error compilation and all 37 tests pass, while this
  host cannot execute the Linux-native dispatch path. Initial Build
  `33939023870` passed package, macOS, and Windows; Ubuntu reached native
  validation and exposed three integration gaps: glslang emitted SPIR-V 1.6
  for a Vulkan 1.1 device, transfer-only images lacked usage compatible with
  the already-supported native views, and an old test assertion still expected
  bind groups to be disabled. The compiler now targets Vulkan 1.1/SPIR-V 1.3,
  view-created images carry an explicitly validated compatible usage, and the
  capability assertions match the promoted compute slice. Replacement Build
  `33939221825` proves the corrected compute path and exact readback in the
  Ubuntu validation lane; package and Windows lanes also pass. Its first macOS
  attempt encountered transient package-consumer output corruption (every
  installed archive and the generated package-version file), despite the same
  commit passing that test locally. The isolated macOS rerun passed without a
  source change, so final Build `33939221825` is green across package, macOS,
  Ubuntu, and Windows.
- `feat/rhi1-vulkan-shader-modules` begins #53 with Vulkan-owned SPIR-V shader
  modules. The backend rejects non-SPIR-V, partial-word, and invalid-magic
  inputs before native creation, copies package bytes into aligned words, and
  destroys modules through the device dispatch under the context lock. The
  Linux native test consumes the deterministic vertex package produced by the
  pinned glslang fixture; the fixture is an explicit CTest dependency. Build
  `33823883160` passes package, macOS, Ubuntu, and Windows; Ubuntu creates the
  native module under Vulkan validation. Local warning-as-error compilation and
  all 37 tests also pass. This checkpoint does not advertise binding, graphics,
  or compute capability and does not close #53.
  The Vulkan backend owns the native module and depends only on RHI's existing
  ShaderPackage-selected `ShaderDesc`; no public contract changes.
- #73's first compiler route is merged by PR #143 at `0e25fd0e`. Final PR-head
  Build `33820207969` passes package, macOS, Ubuntu, and Windows. Issue #73
  remains open for its other accepted compiler routes and backend-consumer
  conformance.
- Issue #55 is closed by PR #142 and merge commit `c249c164`. Its native Build
  `33818770807` and final PR-head Build `33818960477` pass package, macOS,
  Ubuntu, and Windows, including Vulkan validation-layer negative paths.
- `feat/rhi1-shaderc-spirv-route` starts #73 with glslang `16.5.0` pinned at
  `a8d28bd082bff18ffbe80996e922b012f915cf07`. The compiler remains private to
  an explicit optional tool flag; offline and package builds perform no fetch.
  The CLI compiles GLSL/GLSL ES to generated SPIR-V variants, records the exact
  compiler version/revision and bounded diagnostics, rejects invalid source,
  and checks byte-for-byte package determinism. Initial Build `33819736596`
  passed package, macOS, and Ubuntu; Windows exposed glslang's child-scope debug
  postfix mismatch under single-config MinGW. The integration now normalizes
  only the three linked glslang artifact postfixes. Replacement Build
  `33819959268` passes package, macOS, Ubuntu, and Windows, including generated
  GLSL/GLSL ES packages, deterministic repetition, invalid-source diagnostics,
  and the existing native backend lanes. Issue #73 remains open for additional
  compiler routes and consuming-backend conformance.
- Issue #129 completed the contributor-neutral planning workflow and the full
  RHI 1 backlog conversion without changing production code.
- The public Truffle Project #5 contains 95 issues with Status, Phase,
  Workstream, Priority, Effort, and Target evidence fields. Completed #26-#32,
  #48-#51, and #129 are `Done`; #52 is `In Progress`; the remaining 82
  accepted items are visible as `Todo`, with later Phase 6/7 work also labeled
  `deferred`.
- Issue #25 structurally owns #26-#35 and #129. Phase epics #33, #34, and #35
  each structurally own 27 detailed sub-issues: #48-#74, #75-#101, and
  #102-#128 respectively.
- Every new work package records context, outcome, scope, non-goals,
  architecture ownership, public-contract effect, dependencies, objective
  acceptance criteria, and required validation/native evidence. Epic bodies
  contain grouped work-breakdown indexes.
- `docs/planning.md`, the planned-work issue form, and the pull-request template
  make durable issue/Project tracking mandatory for accepted implementation and
  deliberate deferral while preserving a public-safe, contributor-neutral
  boundary.
- Repository automation follow-ups #131 and #132 record the companion-dispatch
  credential failure and deprecated action runtimes as Program/Governance work.
  They contain no credentials or private machine details.
- Issue #48 reconciles the public status documents and backend READMEs with the
  merged Vulkan, D3D12, OpenGL, and OpenGL ES resource slices. The public support
  rows now report validation evidence truthfully and link exact source commits
  and native CI runs.
- Issue #49 implements D3D12 device-local textures, native descriptor views,
  per-subresource state transitions, and padded-row buffer/texture plus
  texture/texture copies. PR #135 Build `33759484321` passes package, macOS,
  Ubuntu, and Windows; the Windows debug-layer WARP lane proves exact padded-row
  upload, texture-to-texture copy, readback, and native view creation. The only
  failing check is the separately tracked companion-routing credential issue
  #131, which is not an engine validation gate.
- Issue #50 implements D3D12 HLSL/DXIL shader ownership, reflected root
  signatures, immutable bind groups and descriptor arrays, graphics/compute
  PSOs, render/depth attachments, MSAA resolve, draw/dispatch, and indirect
  commands. Final Build `33763713016` passes package, macOS, Ubuntu, and the
  Windows debug-layer WARP suite with exact triangle, vertex-input/blend,
  textured descriptor-array/dynamic-offset output, push-constant depth
  ordering, indexed/instanced/indirect draws, MRT, MSAA-resolve, and
  compute-to-render output plus negative capability/layout cases. The separate
  companion-routing failure remains tracked by #131 and is not an engine gate.
- Issue #50 is closed by PR #136 and merge commit `ba3d8a40`; its final receipt
  Build `33763990063` passes package, macOS, Ubuntu, and Windows.
- Issue #51 is complete. It maps RHI buffer/texture and aliasing barriers to
  native D3D12 legacy barriers, gives each timeline semaphore a native fence,
  and encodes queue waits/signals. The public query contract now records
  timestamps, scoped occlusion queries, and explicit query resolution; Null
  validates the lifecycle and D3D12 owns timestamp/occlusion heaps plus exact
  readback. The first Windows build exposed one missing read-only-depth enum
  case, then the native test correctly showed that an `undefined` old layout is
  a wildcard rather than a stale state; both test/code findings are corrected.
  Build `33766645262` passes package, macOS, Ubuntu, and Windows; its WARP lane
  proves timestamp ordering and an exact 64-sample occlusion result. Explicit
  fence timeout/retry coverage and GPU-based validation pass in final Build
  `33766961283` along with package, macOS, and Ubuntu. The full local build and
  36/36 tests also pass.
- Issue #52 implements host-owned Win32 surfaces and DXGI flip-model
  swapchains, borrowed D3D12 back buffers, timeline-connected presentation,
  aligned resize/recreation, tearing capability checks, and typed recovery
  statuses. PR #138 Build `33815232612` passes package, macOS, Ubuntu, and
  Windows; its WARP presentation lane proves exact BGRA8 swapchain readback,
  acquire/render/present, client-size drift, occluded-desktop handling,
  `ResizeBuffers`, destroyed-window detection, and device-loss propagation.
- Issue #52 is closed by PR #138 and merge commit `66db0030`; final PR-head
  Build `33815897273` passes package, macOS, Ubuntu, and Windows.
- `feat/rhi1-vulkan-resources` begins #55 by expanding Vulkan images from the
  narrow 2D slice to capability-checked 1D, 2D, 3D, cube, array, mipmapped,
  compressed, and multisampled device-local shapes with compatible views.
  Native transfer recording now covers mip/layer/volume/compressed copies,
  color/depth/stencil whole-subresource clear and readback, color resolve, and
  nearest/linear color blit. The strict local build and all 36 tests pass.
  PR #140 Build `33817112365` passes package, macOS, Ubuntu, and Windows; its
  Ubuntu validation lane requires BC1, depth/stencil, four-sample resolve, and
  linear-blit exact output rather than silently skipping them. Host-visible
  textures, external-memory decisions, and allocation accounting remain in
  #55.
- `feat/rhi1-vulkan-host-memory` continues #55 with runtime-queried linear
  images for upload/readback memory. Direct texture access follows native
  subresource row/depth pitches, preserves caller padding, flushes/invalidates
  non-coherent memory, and transitions queue work to host access. The Linux
  test requires an exact padded host-upload -> native image copy ->
  host-readback round trip and verifies logical budget release. PR #141 Build
  `33817918711` passes package, macOS, Ubuntu, and Windows, including the Linux
  validation-layer proof. External
  sharing remains deliberately unsupported because no Linux handle ownership
  contract is yet exposed by the backend.
- `feat/rhi1-vulkan-resource-audit` closes the remaining #55 acceptance gaps:
  overflow-safe host layout bounds, deterministic invalid cube-view and
  host-multisample rejection, native partial-clear rejection, budget exhaustion,
  and explicit sampler/external capability gates. Samplers remain sequenced with
  #53 because the public contract exposes them through ordinary bindings; device
  addresses are not exposed by RHI 1; external handles remain unadvertised until
  a platform ownership contract exists. Commit `0c8ec99` passes the strict local
  build and all 36 tests. PR #142 Build `33818770807` passes package, macOS,
  Ubuntu, and Windows; the Ubuntu validation lane exercises the native negative
  paths. This completes #55 without promoting Vulkan beyond `native_smoke`.

- PR #46 is merged without closing issue #33. Vulkan buffer/texture and D3D12
  buffer transfers are on `develop`; post-merge build run `33261566114` is green
  on package, macOS, Ubuntu, and Windows.
- The merged matrix slice publishes `BackendMaturity`, `PlatformKind`,
  per-dimension evidence, and the full backend/platform table through the public
  RHI. Runtime `AdapterInfo` carries its platform and maturity separately.
- `truffle-rhi-doctor` emits the repository matrix plus live adapter probes as
  JSON. Strict mode rejects a built host row at `native_smoke` when native
  initialization fails, and verifies that Null remains validation-only and
  non-native.
- Vulkan-Headers `vulkan-sdk-1.4.350.1` and volk `1.4.350` are pinned submodules
  compiled privately into `truffle_backend_vulkan`; missing source fails with an
  exact submodule recovery command and configure never downloads it.
- Vulkan handles validation-layer discovery and portability enumeration/subset,
  creates a native instance/device/graphics queue, records and submits a native
  command buffer, and exposes an adapter only after completion. Linux is the
  only proposed `native_smoke` lane in this slice.
- Windows D3D12 uses the Windows SDK to create the DXGI WARP adapter, device,
  queue, fence, allocator, and native command list, and waits for execution
  before adapter exposure.
- Linux OpenGL and OpenGL ES use surfaceless EGL pbuffers and deterministic
  clear/readback output before adapter exposure. Their native contexts are kept
  alive for empty RHI queue smoke. Resource and pipeline capabilities remain
  empty rather than simulated.
- WebGPU and WebGL2 have explicit source targets. WebGPU remains unavailable;
  WebGL2 contains an Emscripten canvas-context path but remains `source_only`
  until browser CI runs it.
- CI is segmented into shared contracts, native smoke with validation, native
  presentation, strict doctor, and parity artifacts. Linux installs Mesa Vulkan,
  validation layers, EGL, desktop GL, and GLES prerequisites; Windows uses WARP.
- The first Ubuntu run found GNU's predefined `linux` macro colliding with the
  initial `PlatformKind` enumerator in installed consumer headers. The C++ name
  is now `linux_host` while its serialized/public display value remains
  `"linux"`.
- GitHub build run `33259668349` passes package archive, macOS, Ubuntu, and
  Windows. Ubuntu proves Vulkan plus both EGL profiles and installed consumers;
  Windows proves the D3D12 WARP path; strict doctor passes on every host.
- `feat/rhi1-vulkan-resources` makes backend resource creation instance-aware
  and binds each Vulkan buffer to the instance/device that allocated it.
  Vulkan buffers now use backend-owned `VkBuffer`/`VkDeviceMemory`, select
  device-local or host-visible memory types, and implement map, unmap, flush,
  invalidate, write, and read for upload/readback domains.
- Vulkan queue submission now records native buffer copies and byte-pattern
  fills. The Linux-only native test uploads deterministic bytes, copies through
  device-local memory, fills a subrange, reads the exact result back, and checks
  mapped invalidation. PR #44's Ubuntu validation-layer lane is the native
  evidence for this result.
- `feat/rhi1-vulkan-textures` adds optimal-tiled, device-local Vulkan 2D images,
  same-format image views, per-mip/layer native layout tracking, buffer-to-image,
  image-to-buffer, and image-to-image copies. Supported uncompressed color and
  depth formats are checked against the physical device before allocation;
  compressed formats, multisampling, non-device-local images, and presentation
  images still fail explicitly.
- PR #45's Linux texture proof uses padded 64-byte rows for an 8x4 RGBA8 image,
  copies buffer -> image -> image -> buffer, and compares the complete readback
  including untouched padding. Color image transfers are the only image-transfer
  capability evidenced in that slice.
- `feat/rhi1-d3d12-buffers` adds committed upload, readback, and default-heap
  buffers tied to the creating WARP device, including map/unmap and direct host
  read/write operations. Native submission records buffer copies with explicit
  default-heap state transitions. Byte fills use transient upload buffers so
  the public operation supports unaligned offsets and sizes rather than exposing
  a D3D12-specific restriction.
- PR #46's Windows proof uploads 67 deterministic bytes, copies through a
  default-heap buffer, overwrites an unaligned 17-byte readback subrange, and
  verifies both direct and mapped output exactly under the D3D12 debug layer.
- `feat/rhi1-egl-buffers` adds buffer ownership and mapping to the shared EGL
  implementation used by desktop OpenGL and OpenGL ES. Copies use native copy
  targets; fills use transient upload buffers, preserving arbitrary byte ranges.
  Both Linux profiles share one exact upload -> device-local -> readback test
  with an unaligned fill and mapped verification.
- PR #47's first Ubuntu run reached both native profiles and found that a
  mapped-at-creation upload flush inherited the most recent global GL target
  binding. The merged write path rebinds its owning buffer before explicit
  flush; Build `33262121061` passes both EGL native profiles.

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
  indirect-count behavior is capability-reported and fails explicitly when
  unavailable.
- Presentation is a queue operation; the host owns native windows/layers and
  application lifecycle.
- Renderer descriptor caching/admission/recovery policy remains above core RHI.
- Bundled pinned source is the default for optional dependencies; configure
  performs no network fetches. Platform SDKs remain explicit prerequisites.
- Backend/platform maturity and feature support are separate. A factory exposes
  a GPU adapter only after native initialization; target presence is not
  evidence. Null is validation-only and never a GPU backend.
- ShaderPackage serialization is canonical and versioned. Runtime compiler
  adapters remain optional and backend-local.
- The Simple RHI remains deferred.

## Validation

Issue #129 planning-workflow validation:

```text
GitHub hierarchy: #25 has 11 children; #33, #34, and #35 have 27
children each; all phase epics link structurally back to #25.
GitHub Project #5: 95 issues; Done 8, Todo 87.
Project metadata: no missing Status, Phase, Workstream, Priority, Effort,
or Target evidence values.
Detailed RHI/governance issue audit: #48-#129 include the required actionable
sections; automation follow-ups #131 and #132 were checked separately.
Ruby YAML parse: both issue-template YAML files pass.
cmake --preset ci
cmake --build --preset ci -j 8
ctest --preset ci --output-on-failure                       # 36/36
MTL_DEBUG_LAYER=1 ctest --preset ci -L 'native-smoke|presentation'
                                                               # 2/2
git diff --check
```

Issue #48 matrix-evidence reconciliation on macOS Apple Silicon:

```text
cmake --build --preset ci -j 8
ctest --preset ci --output-on-failure                       # 36/36
MTL_DEBUG_LAYER=1 ctest --preset ci -L 'native-smoke|presentation'
                                                               # 2/2
truffle-rhi-doctor --json /tmp/truffle-rhi48-doctor.json
local Markdown link targets                                  # valid
git diff --check
```

The doctor output records `validation: true` for Linux Vulkan, Windows D3D12,
Linux OpenGL, and Linux OpenGL ES while retaining `native_smoke` maturity and
false conformance/presentation dimensions.

Issue #49 local cross-platform contract validation:

```text
cmake --build --preset ci -j 8
ctest --test-dir build/ci -R truffle_direct3d_tests --output-on-failure
git diff --check
```

The local lane verifies the non-Windows unavailable contract. PR #135 Build
`33759484321` supplies the Windows SDK compilation, WARP execution, exact
256-byte-padded row readback, native texture-view creation, and debug-layer
validation receipt required for merge.

Issue #50 local cross-platform contract validation during implementation:

```text
cmake --build --preset ci --parallel 4
ctest --preset ci --output-on-failure  # 36/36
git diff --check
```

The local macOS lane compiles the shared callback/context changes and verifies
all non-Windows contracts. Final Build `33763713016` passes package, macOS,
Ubuntu, and Windows. Its WARP debug-layer lane executes the complete #50
golden-output and negative-capability suite.

The isolated worktree required the repository's pinned Vulkan submodules; the
documented `git submodule update --init vendor/vulkan-headers vendor/volk`
recovery path succeeded before the clean configure/build/test run.

Current #33 matrix-slice validation on macOS Apple Silicon:

```text
cmake --preset ci
cmake --build --preset ci -j 8
ctest --preset ci -LE 'native-smoke|presentation|validation'  # 34/34
MTL_DEBUG_LAYER=1 ctest --preset ci -L native-smoke           # 1/1
MTL_DEBUG_LAYER=1 ctest --preset ci -L presentation           # 1/1
git diff --check

cmake -S . -B build/gcc-rhi33 -G Ninja \
      -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15 \
      -DTRUFFLE_BUILD_BACKEND_METAL=OFF \
      -DTRUFFLE_BUILD_BACKEND_VULKAN=ON \
      -DTRUFFLE_BUILD_BACKEND_DIRECT3D=ON \
      -DTRUFFLE_BUILD_BACKEND_OPENGL=ON \
      -DTRUFFLE_BUILD_BACKEND_OPENGLES=ON \
      -DTRUFFLE_BUILD_BACKEND_WEBGPU=ON \
      -DTRUFFLE_BUILD_BACKEND_WEBGL2=ON \
      -DTRUFFLE_BUILD_EXAMPLES=OFF \
      -DTRUFFLE_WARNINGS_AS_ERRORS=ON
cmake --build build/gcc-rhi33 -j 8
ctest --test-dir build/gcc-rhi33 --output-on-failure  # 30/30
```

Clang and GCC warning-as-error builds pass locally with the EGL buffer slice,
as do the 34-test segmented shared suite and Metal native smoke. GitHub run
`33261275493` passes the preceding package, macOS Metal, Ubuntu
Vulkan/EGL, and Windows D3D12/WARP with strict doctor enabled. The local sandbox
does not expose a Metal device to strict doctor, so GitHub's macOS runner is the
live-device gate. Ubuntu CI remains the compile and native acceptance gate for
the shared GL/GLES implementation. `truffle_format_check` remains unavailable
because CMake does not discover `clang-format` on this host's `PATH`;
`git diff --check` passes.

## Next Resume Steps

1. Continue #33 through its next dependency-ready focused Project sub-issue;
   update issue and
   Project state whenever scope, evidence, or disposition changes.
2. Keep WebGPU/WebGL2 and every unexecuted mobile/Apple/Vulkan platform at
   `source_only`; keep native slices at `native_smoke` until shared native
   contracts and presentation evidence exist.
3. Keep hosted validation/publication disabled unless explicitly requested.
   Use focused local checks; do not resume cloud-companion credential setup.
4. #132 remains a separate runtime-modernization follow-up for retained hosted
   actions, not a reason to restore automatic CI or change engine maturity.

## Open Risks

- Submission is currently synchronous. It proves completion but does not yet
  provide asynchronous queue throughput or full deferred destruction.
- Metal barrier mapping relies on encoder boundaries and default resource hazard
  tracking; aliased heap allocations are not implemented.
- Physical Metal device-removal evidence is not available in macOS CI; the
  private fault hook validates public loss-state behavior only.
- Vulkan submission and presentation are synchronous correctness paths; async
  throughput and deferred destruction remain later performance work. Linux WSI
  currently supports XCB only. Wayland, Windows, Android, and MoltenVK surface
  adapters remain open platform-specific work. EGL still proves only its
  currently documented resource slice and surfaceless presentation baseline.
- D3D12 submission remains synchronous and fill commands allocate transient
  upload resources per operation. Compressed, host-visible, external, and
  copy-encoder clear/resolve/blit texture paths remain unsupported; multisampled
  2D render/depth attachments and render-pass resolve are supported. Dynamic
  depth bias, bindless tables, tessellation, indirect-count execution, and
  pipeline caches remain explicit unsupported results. Pooling and asynchronous
  retirement belong to later performance slices after correctness is evidenced.
- Linux EGL context destruction is thread-sensitive. The synchronous matrix
  slice serializes and restores the context; asynchronous GL work needs a
  deliberate context-ownership model.
- MoltenVK, Emdawnwebgpu, VMA, D3D12MA, and GL loader/state-tracker groups are
  absent until the code that consumes them is implemented and validated.
- Hosted workflows remain disabled while `master` retains legacy definitions.
  Explicit manual runs must use refs containing the manual-only policy; do not
  re-enable retired companion/setup workflows or restore their secret.
  Issue #132 separately tracks migration away from deprecated Node.js 20 action
  runtimes.

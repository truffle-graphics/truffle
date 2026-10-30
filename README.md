# Truffle

Truffle is an embeddable C++ graphics engine and rendering system for tools,
simulations, CAD-style applications, games, and other graphics-heavy software.

## Status

- Stage: Draft
- RHI: Breaking RHI 1 replacement in progress under milestone `Truffle RHI 1`
- Owner: TinMan
- License: Apache-2.0
- Visibility: Public
- Reason: Truffle is public engineering work and this repository contains the
  project baseline intended for outside review.
- Promotion criteria: Each backend-platform pair is promoted independently from
  native output, validation, presentation, recovery, and packaging evidence.

## What This Project Is

Truffle exists to give graphics-heavy applications a reusable ECS, renderer data
flow, and GPU abstraction without forcing them into a game engine or application
framework. It is not an application framework and it is not a dedicated game
engine.

The preliminary RHI has been replaced without a compatibility shim. Shared
contract tests remain useful, while native maturity is reported independently
for each backend/platform pair:

- `truffle_base` owns shared status, configuration, and handle primitives.
- `truffle_assets` defines declarative asset, material-operation, texture,
  geometry stream, and group/tag metadata without owning file loading or GPU
  upload.
- `truffle_asset_render` turns declared asset streams into metadata-only render
  layout and batch plans without owning buffers, uploads, shaders, or backends.
- `truffle_encom` provides Truffle EnCom (ENtity COMponent), a general-purpose
  entity-component world.
- `truffle_rhi` contains the modular RHI 1 object, dispatch, command,
  synchronization, and presentation foundation.
- `truffle_backend_null` strictly validates RHI 1 lifetime and state without a
  GPU dependency.
- `truffle_backend_metal` implements native buffer and selected 2D texture
  allocation, views, mapping/coherence, copy/fill, upload, readback, immutable
  bindings, graphics/compute pipelines, shared-event synchronization, and
  `CAMetalLayer` presentation. Validation-enabled exact resource, triangle,
  textured, depth/MRT/MSAA, indirect, compute-to-render, mixed-ordering, and
  acquire/present proofs reach `native_smoke` on macOS.
- `truffle_backend_vulkan` uses pinned private Vulkan headers and volk. Linux
  initializes a real loader/device/graphics queue, owns upload/readback and
  device-local buffers plus host-visible linear images and capability-checked
  1D, 2D, 3D, cube, array, mipmapped, compressed, and multisampled
  textures/views, and accepts ShaderPackage-selected SPIR-V as native shader
  modules. Native evidence covers padded-row and subresource copies, clear,
  resolve, nearest/linear blit paths, immutable descriptor-backed bindings,
  push constants, and direct compute dispatch with exact storage-buffer
  readback. Generated vertex/fragment SPIR-V also drives a native single-color
  graphics pipeline with exact direct, indexed, and indirect render-target
  output. Reflected sampled-texture/sampler bindings produce exact texel output,
  indirect compute reproduces the direct storage-buffer result, depth testing
  proves exact pass/fail output, two render targets produce independent exact
  colors, four-sample rendering resolves to exact output, and compute-to-render
  ordering produces an exact storage-driven fragment result. Discovered
  graphics/compute/transfer queues execute timeline-semaphore-connected work;
  explicit buffer, texture, and aliasing barriers plus native timestamp and
  occlusion query resolves are validation-tested. Linux additionally consumes
  host-owned XCB handles for native acquire/render/present, exact swapchain
  readback, and resize recreation under a validation-enabled Xvfb lane.
- `truffle_backend_direct3d` initializes the Windows SDK D3D12 WARP device,
  owns upload/readback and default-heap buffers plus device-local textures and
  descriptor views, consumes packaged native HLSL or offline DXIL, maps logical
  layouts to root signatures, and owns graphics/compute pipelines, barriers,
  timeline synchronization, timestamp/occlusion queries, and host-owned Win32
  DXGI presentation. GPU-validation WARP tests prove ordered native work,
  query and swapchain readback, resize/recovery, exact triangle, textured
  binding, depth/MRT/MSAA, indexed/instanced/indirect, and compute-to-render
  output.
- `truffle_backend_opengl` and `truffle_backend_opengles` use Linux surfaceless
  EGL contexts and deterministic clear/readback smoke proofs. Both own mapped
  and device-local buffers/views, samplers, uncompressed color/depth/stencil
  textures, mip/array/3D/cube transfers, exact clears, copies, and blits.
  Both consume reflected native GLSL ShaderPackage variants with logical
  remaps, sampled arrays, dynamic offsets, immutable samplers, push blocks and
  scalar specialization equivalents. Graphics includes vertex/index input,
  instancing, depth/stencil, MRT and dynamic state. Desktop GL additionally
  owns core texture views, MSAA resolve, indirect drawing and compute-to-render;
  GLES 3.0 gates those paths and supports identity texture views for sampling.
  Storage images, tessellation, bindless, indirect-count, asynchronous
  synchronization and presentation remain unsupported. Both stay `native_smoke`.
- WebGPU remains an explicit `source_only` factory. WebGL2 has a browser-native
  context path when built with Emscripten but remains `source_only` until its
  browser CI evidence exists. Neither exposes a simulated adapter.
- `ShaderPackage` provides a deterministic compiler-free runtime container with
  normalized cross-target reflection and native-variant selection. The optional
  `truffle-shaderc` target assembles and inspects packages without adding a
  compiler dependency to RHI consumers.
- `truffle_shader` owns optional source compilation, route discovery, typed
  target configuration, reflection, dependency tracking, caching, and rebuild
  results. Shaderc and Renderer use this library directly; RHI never depends
  on it.
- `truffle_renderer` starts the independently consumable rendering layer above RHI.
- `truffle_encom_renderer` is the optional EnCom–Renderer integration that
  extracts EnCom state into render batches.
- `truffle_diagnostics` provides opt-in asset/render/frame/asset-render-plan/
  debug-overlay inspection helpers and bundle reports without adding diagnostics
  dependencies back into Renderer or EnCom–Renderer code.

The renderer layer will accept more than one data path. ECS extraction stays a
first-party convenience path, while large dynamic workloads can grow toward
bulk, data-oriented, or GPU-resident render data without requiring per-entity
conversion.

Truffle has no dependency on an application or host framework. A consumer may
compose Truffle with one at its own application boundary.

## Current Focus

Current focus is **Shader completion first** under #73. Route/target contracts
(#185, PR #194) and baseline reflection (#186, PRs #195/#196) are completed.
Native MSL authoring and offline Metal libraries (#188) are the current bounded
phase: optional in-process Metal reflection, ordered source-unit composition,
typed interface validation and a separate installed-toolchain metallib route.
Composed native source and metallib each pass exact Apple M3 Pro graphics and
compute readback; see the Shader matrix for macOS/API floors and declaration
limits. Portable native output (#187) separately provides experimentally gated
GLSL/GLSL ES-to-MSL 2.1, GLSL 410 and GLSL ES 300 with real logical/native
entry identities and binding remaps. The unreleased TSP draft retains its
`1.0` identifiers without compatibility scaffolding.
The merged #178-#181 Shader/Renderer foundations are
not full module completion. Renderer feature work is paused; the remaining
Shader phases (#187-#193) and their evidence gates are in
[`docs/roadmap.md`](docs/roadmap.md). See the
[Shader API and implemented/planned route matrix](src/shader/README.md) for
accepted sources, emitted targets, target versions, and native evidence.

## Build

Prerequisites:

- CMake 3.23 or newer.
- A C++20-capable toolchain.
- Ninja when using the checked-in CMake presets.
- Checked-out optional backend dependencies (`git submodule update --init`).
- A C compiler and the native window-system development dependencies needed by
  the copied GLFW source when examples are enabled.

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The checked-in presets build the optional `truffle-shaderc` package assembler
and its pinned offline GLSL/GLSL ES to SPIR-V route. Other consumers can opt in
to the dependency-free assembler with `-DTRUFFLE_BUILD_SHADERC=ON`, then enable
the Shader module's glslang adapter explicitly with
`-DTRUFFLE_SHADER_ENABLE_GLSLANG=ON`; the runtime RHI never gains a compiler
dependency.
Portable native outputs additionally require
`-DTRUFFLE_SHADER_ENABLE_SPIRV_TOOLS=ON -DTRUFFLE_SHADER_ENABLE_SPIRV_CROSS=ON`.
Explicitly prepare their pinned submodules first; configure never downloads.
Native MSL source/reflection is independently enabled with
`-DTRUFFLE_SHADER_ENABLE_METAL_COMPILER=ON` on Apple. Offline `metallib` output
additionally needs `-DTRUFFLE_SHADER_ENABLE_METALLIB_COMPILER=ON` and the
installed Xcode Metal Toolchain. These flags are disabled by default; prepared
package consumers do not link the compiler.
See the [native MSL and portable Shader target profiles and limits](src/shader/README.md#native-msl-source-and-libraries-188)
and [shaderc examples](docs/rhi1/shader-package.md#tool-usage).

`truffle-rhi-doctor --strict --json <path>` records the declared matrix and the
adapters that actually initialized on the current host. Strict mode fails when
a built backend with a `native_smoke` host claim cannot expose its native
adapter. Null is reported separately as validation-only.

See `docs/charter.md`, `docs/architecture.md`, `docs/roadmap.md`, and
`docs/rhi1/` for the current boundaries, replacement sequence, and backend
maturity evidence.

Accepted and deliberately deferred work is tracked in the
[Truffle delivery project](https://github.com/users/loosewired/projects/5) and
repository issues. [`docs/planning.md`](docs/planning.md) defines the durable
planning and traceability rules used by contributors and automation.

## CMake Consumers

Truffle keeps each library layer linkable on its own. Install the configured
workspace and import the package from a consumer:

```cmake
find_package(Truffle CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE Truffle::RHI Truffle::Renderer)
```

The current package exports `Truffle::Base`, `Truffle::Assets`,
`Truffle::AssetRender`, `Truffle::EnCom`, `Truffle::RHI`, `Truffle::Shader`,
`Truffle::BackendNull`, `Truffle::Renderer`, `Truffle::EnComRenderer`, and
`Truffle::Diagnostics`. Additional backend exports
(`Truffle::BackendMetal`, `Truffle::BackendVulkan`, `Truffle::BackendOpenGL`,
`Truffle::BackendDirect3D`) are available when their corresponding CMake
options are enabled. OpenGL ES, WebGPU, and WebGL2 are exported as
`Truffle::BackendOpenGLES`, `Truffle::BackendWebGPU`, and
`Truffle::BackendWebGL2`. Target presence is never itself a support claim; use
the public backend/platform matrix and the RHI doctor evidence.

See `docs/distribution.md` for package generation, install verification, and
release workflow guidance.

## Continuity And AI Guidance

Cross-machine working state belongs in [`docs/handoff.md`](docs/handoff.md).
AI sessions should start with [`AGENTS.md`](AGENTS.md) and
[`AI_CONTEXT.md`](AI_CONTEXT.md). The local doctrine snapshot lives under
[`docs/doctrine/`](docs/doctrine/README.md).

## Attribution

If you use Truffle in your project, please mention Truffle and credit
TinMan / George Gil in your project documentation.

## Use

Embed only the targets your application needs. Host-owned window loops can link
the RHI and renderer targets directly and provide their own native surface
boundary.

## Repository Layout

- `include/` public Truffle contracts
- `src/` module implementations and backend ownership
- `cmake/` package, install, warning, option, and developer-tooling helpers
- `tests/` contract tests
- `examples/` host-owned workspace integration proof plus dense-data
  diagnostics examples
- `docs/` charter, architecture, backend roadmap, handoff, and doctrine snapshot

## Contributing

See `CONTRIBUTING.md` for the branch workflow, commit format, signing
expectations, pull request path into protected `develop`, and stable promotion
path into `master`. Development is local-first with focused checks and normal
Git Flow merge commits. Hosted builds/releases are manual opt-ins; ordinary
PRs and pushes do not launch full CI or cloud-agent companions.

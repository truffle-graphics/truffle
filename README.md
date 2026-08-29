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
contract tests remain useful, but only Metal currently calls a native graphics
API:

- `truffle_core` owns shared status, configuration, and handle primitives.
- `truffle_assets` defines declarative asset, material-operation, texture,
  geometry stream, and group/tag metadata without owning file loading or GPU
  upload.
- `truffle_asset_render` turns declared asset streams into metadata-only render
  layout and batch plans without owning buffers, uploads, shaders, or backends.
- `truffle_ecs` provides a general-purpose ECS world.
- `truffle_rhi` contains the modular RHI 1 object, dispatch, command,
  synchronization, and presentation foundation.
- `truffle_backend_null` strictly validates RHI 1 lifetime and state without a
  GPU dependency.
- `truffle_backend_metal` implements native buffer and selected 2D texture
  allocation, views, mapping/coherence, copy/fill, upload, readback, immutable
  bindings, and graphics/compute pipelines. Validation-enabled exact resource,
  triangle, textured, depth/MRT/MSAA, indirect, and compute-to-render proofs
  reach `native_smoke` on macOS; synchronization and presentation remain.
- `truffle_backend_vulkan`, `truffle_backend_opengl`, and
  `truffle_backend_direct3d` expose explicit unavailable factories until their
  native implementations land; they report no simulated adapters.
- `ShaderPackage` provides a deterministic compiler-free runtime container with
  normalized cross-target reflection and native-variant selection. The optional
  `truffle-shaderc` target assembles and inspects packages without adding a
  compiler dependency to RHI consumers.
- `truffle_render` starts the independently consumable rendering layer above RHI.
- `truffle_diagnostics` provides opt-in asset/render/frame/asset-render-plan/
  debug-overlay inspection helpers and bundle reports without adding diagnostics
  dependencies back into render or scene code.

The renderer layer will accept more than one data path. ECS extraction stays a
first-party convenience path, while large dynamic workloads can grow toward
bulk, data-oriented, or GPU-resident render data without requiring per-entity
conversion.

Truffle has no dependency on FrameKit. A consumer may compose both projects in
the same application when an application host and graphics system are useful
together.

## Build

Prerequisites:

- CMake 3.23 or newer.
- A C++20-capable toolchain.
- Ninja when using the checked-in CMake presets.
- A C compiler and the native window-system development dependencies needed by
  the copied GLFW source when examples are enabled.

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The checked-in presets build the optional `truffle-shaderc` package assembler.
Other consumers can opt in with `-DTRUFFLE_BUILD_SHADERC=ON`; the runtime RHI
never gains a compiler dependency.

See `docs/charter.md`, `docs/architecture.md`, `docs/roadmap.md`, and
`docs/rhi1/` for the current boundaries, replacement sequence, and backend
maturity evidence.

## CMake Consumers

Truffle keeps each library layer linkable on its own. Install the configured
workspace and import the package from a consumer:

```cmake
find_package(Truffle CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE Truffle::RHI Truffle::Render)
```

The current package exports `Truffle::Core`, `Truffle::Assets`,
`Truffle::AssetRender`, `Truffle::ECS`, `Truffle::RHI`,
`Truffle::BackendNull`, `Truffle::Render`, `Truffle::Scene`, and
`Truffle::Diagnostics`. Additional backend exports
(`Truffle::BackendMetal`, `Truffle::BackendVulkan`, `Truffle::BackendOpenGL`,
`Truffle::BackendDirect3D`) are available when their corresponding CMake
options are enabled. Except for Metal, those optional backend exports currently
return `unsupported`; their presence is not a native support claim.

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
path into `master`.

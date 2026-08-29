# RHI 1 Dependency Policy

RHI 1 keeps the core dependency-free beyond C++20 and the standard library.
Backend, shader-tool, test-runtime, and example dependencies are distinct build
groups and are enabled only by the targets that use them.

## Acquisition Rules

1. Optional source dependencies use pinned Git submodules by default.
2. CMake configure and build never fetch from the network.
3. Enabling a target with a missing bundled dependency fails with the exact
   submodule or SDK prerequisite and a recovery command.
4. A deliberate expert system mode may resolve compatible installed packages;
   it must report the resolved version and path and must not silently fall back
   from bundled to system source.
5. Every bundled dependency records revision, upstream, license, patches, and
   the Truffle targets that consume it.
6. Disabled backends do not add their headers, libraries, tools, or transitive
   package requirements to `truffle_rhi` consumers.

The intended configuration boundary is one explicit dependency mode (bundled
by default, system only by request), plus backend-specific enable flags. It is
introduced alongside the first dependency-bearing native backend rather than
as an unused framework.

## Dependency Groups

### Core runtime

`truffle_core` and `truffle_rhi` use no third-party runtime source dependency.

### Native backends

- Metal uses the Apple SDK's Metal, QuartzCore, and Foundation frameworks.
- Vulkan may use pinned Vulkan headers/loader integration and Vulkan Memory
  Allocator; MoltenVK is an Apple-only optional portability dependency.
- D3D12 uses the Windows SDK and DXGI; D3D12 Memory Allocator and an optional
  pinned Agility SDK runtime are backend-local.
- WebGPU uses pinned Emdawnwebgpu/Dawn components only when that backend is
  enabled.
- OpenGL-family targets use a pinned loader where needed and platform-provided
  GL, EGL, GLES, or Emscripten interfaces.

### Shader tooling

The optional dependency-free `truffle-shaderc` assembly target consumes
precompiled or backend-native variants without a compiler library. Future
source-to-target routes may use pinned Slang, DXC, glslang, SPIR-V Tools,
SPIRV-Cross, and Tint submodules, enabled individually. No compiler submodule is
introduced before its route exists, and configure never downloads it. Runtime
packages do not acquire the compiler toolchain. Runtime source compilation is
an explicit optional feature; production packages prefer offline variants.

### Validation and tests

Validation layers, Vulkan software drivers, D3D12 WARP, Mesa llvmpipe/EGL,
browser runners, and native platform simulators are CI/runtime prerequisites,
not libraries linked into core RHI. Backend diagnostics must make a missing
validation runtime visible.

### Examples

Window and UI dependencies remain example-local. They do not define RHI's host
boundary and are not exported to library consumers.

## Unavoidable SDK Prerequisites

Bundled source cannot replace the platform toolchain. Enabled targets may
require:

- Xcode and the matching Apple platform SDK for Metal targets;
- Windows SDK 19041 or newer for D3D12 targets;
- Android NDK and platform API 26 or newer for Android targets;
- Emscripten for wasm32 WebGPU/WebGL2 targets;
- driver/runtime packages needed for native Linux Vulkan, GL, GLES, and EGL
  execution.

Configure diagnostics and the future RHI doctor command must distinguish a
missing SDK, a missing checked-out submodule, and an incompatible expert system
package. No diagnostic may download or mutate the machine without a separate,
explicit apply step.

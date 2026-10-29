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

### Base and RHI runtime

`truffle_base` and `truffle_rhi` use no third-party runtime source dependency.

### Native backends

- Metal uses the Apple SDK's Metal, QuartzCore, and Foundation frameworks.
- Vulkan currently compiles `Vulkan-Headers` `vulkan-sdk-1.4.350.1` and volk
  `1.4.350` directly into `truffle_backend_vulkan`; both are pinned Git
  submodules and remain private to that target. The host supplies only the
  native loader/driver runtime. Vulkan Memory Allocator remains a future
  backend-private group and is not added before its path exists.
- Vulkan on macOS uses MoltenVK (Apache-2.0) and the Khronos Vulkan loader
  (Apache-2.0) as host runtime prerequisites; neither is bundled or pinned.
  Recorded evidence used Homebrew `molten-vk` 1.4.2 and `vulkan-loader`
  1.4.363.0 (`vulkan-tools` 1.4.363.0 supplied `vulkaninfo`). The loader
  finds the MoltenVK ICD from its own manifest search. Because dyld does not
  search the Homebrew prefix, the backend falls back to
  `/opt/homebrew/lib/libvulkan.1.dylib` and then
  `/usr/local/lib/libvulkan.1.dylib` after volk's default search fails;
  `DYLD_FALLBACK_LIBRARY_PATH` or an SDK install also works. Khronos
  validation layers (`vulkan-validationlayers`) are optional and were not
  present for the first macOS run.
- D3D12 uses the Windows SDK and DXGI; D3D12 Memory Allocator and an optional
  pinned Agility SDK runtime are backend-local.
- WebGPU uses pinned Emdawnwebgpu/Dawn components only when that backend is
  enabled.
- OpenGL-family targets use a pinned loader where needed and platform-provided
  GL, EGL, GLES, or Emscripten interfaces.

### Shader tooling

The optional dependency-free `truffle-shaderc` assembly target consumes
precompiled or backend-native variants without a compiler library. Its first
source route is enabled independently with `TRUFFLE_SHADERC_ENABLE_GLSLANG` and
compiles GLSL or GLSL ES to SPIR-V using the pinned `vendor/glslang` source.
Future routes may use pinned Slang, DXC, SPIR-V Tools, SPIRV-Cross, and Tint
submodules, enabled individually. No compiler submodule is introduced before
its route exists, and configure never downloads it. Runtime packages do not
acquire the compiler toolchain. Runtime source compilation is an explicit
optional feature; production packages prefer offline variants.

| Dependency | Pin | Upstream and license | Consumer and update policy |
|---|---|---|---|
| glslang | `16.5.0` / `a8d28bd082bff18ffbe80996e922b012f915cf07` | `KhronosGroup/glslang`; upstream `LICENSE.txt` contains BSD-2-Clause, BSD-3-Clause, MIT, and Apache-2.0 notices. | Private to `truffle-shaderc` when its GLSL route is enabled. No local patches. Update by reviewing release notes and licenses, advancing the submodule, then running deterministic compile, invalid-source, package, and native Vulkan consumers. |

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
  execution;
- MoltenVK and a Vulkan loader for Vulkan on macOS.

Configure diagnostics and the RHI doctor command distinguish a
missing SDK, a missing checked-out submodule, and an incompatible expert system
package. No diagnostic may download or mutate the machine without a separate,
explicit apply step.

The RHI doctor is now implemented. Missing bundled Vulkan source fails at
configure with the exact submodule command; missing loaders, devices, SDKs, or
contexts are reported as runtime probe status in its JSON output.

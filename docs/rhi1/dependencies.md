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
  validation layers are optional at runtime; the `native_smoke` evidence used
  Homebrew `vulkan-validationlayers` 1.4.363.0. Its manifest names
  `libVkLayer_khronos_validation.dylib` without a path, so the loader cannot
  load the layer unless dyld can search the Homebrew prefix. The macOS Vulkan
  ctest therefore sets `DYLD_FALLBACK_LIBRARY_PATH` to the directory of the
  host loader that CMake finds; direct runs need the same variable.
- D3D12 uses the Windows SDK and DXGI; D3D12 Memory Allocator and an optional
  pinned Agility SDK runtime are backend-local.
- WebGPU uses pinned Emdawnwebgpu/Dawn components only when that backend is
  enabled.
- OpenGL-family targets use a pinned loader where needed and platform-provided
  GL, EGL, GLES, or Emscripten interfaces.

### Shader tooling

The optional dependency-free `truffle-shaderc` assembly target consumes
precompiled or backend-native variants without a compiler library. Its first
source route is enabled independently with `TRUFFLE_SHADER_ENABLE_GLSLANG` and
compiles GLSL or GLSL ES to SPIR-V using the pinned `vendor/glslang` source.
Portable native routes use `TRUFFLE_SHADER_ENABLE_SPIRV_CROSS`, requiring
glslang and `TRUFFLE_SHADER_ENABLE_SPIRV_TOOLS`. The independent native Slang route
uses `TRUFFLE_SHADER_ENABLE_SLANG` plus SPIRV-Tools; its clean pinned source
closure, Apache-2.0 WITH LLVM-exception license, source-only offline policy,
install behavior and update checks are recorded in
[native Slang](../shader-slang.md). Future routes may use DXC and Tint
submodules, enabled individually. No compiler submodule is introduced before
its route exists, and configure never downloads it. Runtime packages do not
acquire the compiler toolchain. Runtime source compilation is an explicit
optional feature; production packages prefer offline variants.

| Dependency | Pin | Upstream and license | Consumer and update policy |
|---|---|---|---|
| glslang | `16.5.0` / `a8d28bd082bff18ffbe80996e922b012f915cf07` | `KhronosGroup/glslang`; upstream `LICENSE.txt` contains BSD-2-Clause, BSD-3-Clause, MIT, and Apache-2.0 notices. | Private to `Truffle::Shader` when its adapter is enabled; shaderc and optional runtime clients share it. Static installs include its exported link closure; adapter-disabled prepared-package RHI/Renderer consumers remain compiler-free. No local patches. Update by reviewing release notes and licenses, advancing the submodule, then running deterministic compile, invalid-source, package/install, and native Vulkan consumers. |
| SPIRV-Cross | `vulkan-sdk-1.4.350.1` / `1a6169566c73d3da552748fc372fe2bbb856e46e` | `KhronosGroup/SPIRV-Cross`; Apache-2.0 primary license, bundled generated-header/registry notices in `LICENSES` (MIT, Khronos Free Use, CC-BY-4.0). | Private optional core/GLSL/MSL static closure. C++ `CompilerMSL` resource remaps/cleansed entry API and `CompilerGLSL` combined-sampler API audited at this pin; no C API, CLI or extra target generators. |
| SPIRV-Tools | `v2026.3` / `b707790a898e44038547df54580022fc1cf89c3d` | `KhronosGroup/SPIRV-Tools`; Apache-2.0 | Private optional in-process `SpirvTools::Validate`, Vulkan 1.0/1.1 environments. Pin is glslang 16.5.0's `known_good.json` selection, not an arbitrary latest tool. Only validation static library is built/installed, exported as Shader's private static closure in Truffle's target export; no optimizer or tool runtime. |
| SPIRV-Headers | `29981f65241605e08b0ede4cfeb999fe3b723c6a` | `KhronosGroup/SPIRV-Headers`; Khronos MIT-style license, MIT/CC-BY-4.0 notices in `LICENSES`; optional JsonCpp utility carries PD/MIT terms in root `LICENSE`. | Paired with Tools by glslang's `known_good.json`; supplied explicitly through `SPIRV-Headers_SOURCE_DIR`. Build-time grammar/header dependency only, not an RHI dependency. |

These pins have no local patches. Prepare all source checkouts explicitly with
`git submodule update --init vendor/glslang vendor/spirv-cross vendor/spirv-tools
vendor/spirv-headers`; configure checks exact clean revisions and never fetches.
Offline builds use those already-prepared sources and the platform SDK/Python
needed for Tools' generated tables. No implicit system fallback is implemented.
Updates review upstream API changes, release notes and all license notices,
change the gitlinks and recorded compiler pins together, then rerun the focused
route/invalid-source/cache/CLI-equivalence/install tests and the generated
Vulkan/Metal/GL native fixture. Tools/Headers update as a compatible pair;
the Cross SDK tag is separately tested against emitted SPIR-V 1.0/1.3.
Enabled installs preserve their license texts under `share/licenses/Truffle`.
Disabling all adapters removes these libraries and package requirements entirely.

Cross uses its upstream C++/SPIR-V namespace overrides (`truffle_spirv_cross`,
`truffle_spirv_cross_spv`) to avoid collisions between its generated SPIR-V
header and glslang/Tools headers, or an application's own translator.
Exceptions stay enabled so unsupported constructs return diagnostics rather
than asserting. Tools' optional mimalloc integration is disabled; it adds no
extra allocator source or package requirement.

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

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
  Windows x64 additionally proves the shared native suite and Win32
  acquire/render/readback/present, resize and recovery on an RTX 4080 SUPER
  with Khronos validation (#265). A `win32` surface borrows its HWND in
  `NativeSurface::handle`; `display` may supply its HINSTANCE or be null to
  derive the owning instance from that window. RHI owns neither the window
  nor its message loop. Minimized/zero-client windows return `out_of_date`
  until restored; destroyed windows return `surface_lost`. The host Vulkan
  runtime and validation layer are prerequisites, not bundled dependencies.
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
  Windows desktop GL now uses WGL GL 4.5 core with private pinned Khronos
  headers and per-context function tables (#266). The shared native suite,
  completion fences, host timeline semaphores, occlusion/timestamp queries,
  worker-thread context transfer/teardown and host-owned HWND presentation
  have local RTX 4080 SUPER/KHR_debug evidence. `win32` surfaces borrow their
  HWND in `NativeSurface::handle`; surface/presentation work stays on its
  owning thread, while resource/submission work is serialized and restores
  the caller's HGLRC/HDC. Windows/event loops remain host-owned. Resize,
  minimize/restore, destroyed windows and replacement presentation are tested.
  This is Windows x64 `native_smoke`, not full conformance or a Linux/GLES
  capability promotion; see the support matrix for exact limits.
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

**The agreed Shader baseline (#73) is implemented**, with final acceptance
recorded by #193. It covers language-native composition, requested
configurations, optional offline/runtime compilation, reflection,
dependencies, caching, rebuilding and shared tools. GLSL/GLSL ES, HLSL, Slang,
WGSL and native MSL have bounded optional routes; this is not every upstream
language feature or every source-to-target combination.

See the [evidence-linked Shader matrix](src/shader/README.md#completion-scope-and-evidence-73-193)
for actual routes, experimental gates, reflection limits and native Apple
Silicon Metal/Vulkan/OpenGL 4.1 results. Other-platform output verification
remains separately tracked; Shader completion does not promote backend
maturity. The unreleased TSP draft retains `1.0` without compatibility
scaffolding.

The minimum flexible Renderer (#206) is implemented:
app-defined vertex/instance layouts and mesh uploads, optional camera/depth
helpers, and a [host-owned Metal window example](examples/minimum_renderer/README.md)
with composed application shaders, lighting and resize. Renderer owns no ECS
schema or fixed transform/light/material model. GUI-readiness primitives add
clip rectangles, sampled textures, streamed geometry and frame pacing. The GUI
module's architecture is accepted in
[ADR 0013](docs/adr/0013-truffle-gui-architecture.md) (#217). `Truffle::GUI`
provides the persistent tree, incremental layout, Renderer drawing (Metal and Vulkan),
host boundary, text, event routing, themes and core widgets (#221-#229),
plus grid layout, placement overrides, canvas drawing and overlays (#230-#231),
a nested C++ builder (#232), immediate-style regions (#233) and virtualized
lists, table rows and tree flattening (#234) and the optional
Truffle GUI Dock library (#235).
The usability follow-up (#251-#253) adds Unicode fallback/bidirectional text,
editable controls and host-owned detached docking windows. Divider hover and
capture request the corresponding resize cursor. Native two-window rendering,
interaction and redocking are demonstrated in
[`examples/gui_dock_window`](examples/gui_dock_window/README.md).
Detached panels keep their live elements in an independent `Surface` of the same
Context. Saved layouts normalize them back into docking; OS window geometry is
not persisted. Native windows remain a host integration, demonstrated on
macOS/Metal rather than claimed for every platform.
The [native widget demo](examples/gui_window/README.md) records idle and
selective-update evidence. Epic #217 (#221-#235) is delivered; this
is a usable baseline, not a finished GUI framework.
The prepared Vulkan Painter program (#268) has native Windows x64 readback
evidence on an RTX 4080 SUPER with required Khronos validation. It preserves
the existing shape/text/texture, clipping, canvas and premultiplied-alpha
contracts without a runtime compiler. Metal remains the macOS path; other
GUI backend/platform execution is not implied. See
[`docs/architecture.md`](docs/architecture.md#gui-prepared-rendering-268)
for package regeneration and focused checks.
Assets, a general material system and later application work are not started.
[`docs/roadmap.md`](docs/roadmap.md) records the review and evidence boundaries.
GUI text support is enabled by default and uses private pinned FreeType,
HarfBuzz, SheenBidi and utf8proc submodules;
`-DTFL_GUI_ENABLE_TEXT=OFF` builds a text-free GUI. No font is
bundled, and applications supply font data with its associated license.

## GUI Object API

GUI input routing (#226) adds clipped topmost hit testing, bubbling callbacks, hover notifications,
keyboard focus/Tab traversal, and per-pointer capture. Register callbacks with
`Context::set_event_handler`; `RoutedEvent::stop_propagation()` stops bubbling
and `prevent_default()` suppresses default Tab traversal. Capture ends on the
final button release, cancellation, disabling, destruction or host focus loss.
`set_event_trace` installs an optional sink; no log is collected by default.
Input uses the last completed layout; prepare a changed tree before targeting it.
Named themes (#227) use `set_theme_value`, `bind_property`,
`set_property_override` and `clear_property_override`. Colors, borders and
radii repaint only their dependents; padding, stack spacing and fonts also
invalidate dependent layout. Overrides shield an element from theme updates.
Two neutral built-in themes, Truffle Dark and Truffle Light (#260), live in
`truffle/gui/themes.hpp`. `apply_theme` switches all tokens; widgets,
`TextEdit` and `DockSpace` call `install_default_theme`, which fills only
missing tokens with Truffle Dark, so application values always win. Dock colours
use `dock.*` tokens; `DockMetrics` holds only geometry, including an optional
`gap` between areas (default 0) that exposes `dock.background`.
`prepare_frame` resolves pending styles before layout and shapes; check its
`status` before drawing (for example, a font unloaded by the application fails
explicitly and keeps work pending). Manual stages call `update_styles` first.
`set_text_source` retains UTF-8 for font rebinding and borrows the application's
`TextEngine`/atlas; direct `set_text` instead supplies already-shaped text.
Text shaping (#251) segments scripts and bidi paragraphs into visual runs.
`TextEngine::set_fallbacks` sets an ordered chain of application-loaded fonts;
the engine performs no OS font discovery. Glyphs retain their font and source
UTF-8 cluster. `TextLayout` exposes visual carets, `hit_test`, `caret_position`
and selection rectangles, while `text_graphemes` validates UTF-8 and returns
extended-grapheme boundaries. Newlines create separate lines; wrapping remains
an application policy.
Theme bindings govern the named style properties; raw layout/decoration setters
remain the low-level API, not implicit overrides of bindings.
Core widgets (#228) are checked `Widget` references created with `WidgetDesc`:
panel, label, image, button, checkbox, slider and scroll area. The Context owns
their elements; destroying a reference does not remove the UI. User actions
invoke the descriptor callbacks; programmatic setters do not. Use `element()`
for layout/theme overrides and `content()` to add children to a scroll area.
Widgets retain semantic roles/labels, not an accessibility implementation.
Reusable menus (#269) are checked `MenuBar`, `MenuButton`, `Menu`, `MenuItem`
and `MenuSeparator` references in `truffle/gui/menus.hpp`.
`MenuBar::add_menu(label)` creates its trigger automatically; a standalone
`MenuButton` creates its dropdown, and `Menu::add_submenu(label)` creates an
arrow-bearing row and recursive menu. Entries have actions or submenus, disabled
state, optional checks and shortcut display. One open chain per surface handles
pointer/keyboard traversal, delayed hover, safe submenu travel, focus and
dismissal; popups flip/shift within the viewport. Application values, actions,
shortcut bindings and native hosts remain outside the widgets. See
[`docs/gui-menus.md`](docs/gui-menus.md) for composition, lifetime, themes and
the compiled host-independent example. Adopting the new Context storage/input
signatures requires rebuilding GUI consumers and libraries together.
The `text_edit` widget (#252) is also available as `TextEdit`, through
`ui::text_edit` in a builder, or as a two-way `RegionScope::text_edit`.
It supports plain single-line/multiline UTF-8, grapheme-safe selection/deletion,
visual arrow navigation, Home/End, clipboard, 128 undo/redo steps and IME
preedit/commit. `Widget::editor()` exposes its checked editing reference.
Programmatic `set_text` resets undo history without calling `text_changed`;
user edits and undo/redo call it. Read-only controls still allow selection/copy.
Line endings normalize to LF; single-line insertion converts line breaks to
spaces. Lines scroll to keep the caret visible; automatic line wrapping and
rich-text editing are not supplied. The caret is steady, not a continuous
redraw timer. Editing state/font are explicit (`TextEdit::set_font`), while
the outer element remains available for normal layout/decoration overrides.
Native IMEs receive `Host::set_text_input_state` and `set_text_input_area`;
replacement/selection ranges use UTF-8 bytes, not glyphs or UTF-16 indices.
Hosts can query `TextEdit::caret_rect()` for current candidate geometry after
ancestor scrolling. The Cocoa demo implements `NSTextInputClient`, including
UTF-16 conversion, marked text and candidate-window placement.
`Context::handle` returns callback errors and `last_event_status` retains them.
Scrolling translates children consistently in drawing, hit testing and
absolute bounds without regenerating layout or shapes.
Contexts are non-copyable and non-movable so callbacks and Widget references
keep a stable owner. Application text engines/registered textures must outlive
the elements using them.

```cpp
truffle::gui::Context ui;
auto result = truffle::gui::Widget::create(ui, ui.root(), {
    .kind = truffle::gui::WidgetKind::button,
    .activated = [] { /* update application state */ },
});
if (!result.ok()) { /* report result.status() */ }
// A caption additionally supplies text, text_engine, font and atlas in WidgetDesc.
```

## Build

Prerequisites:

- CMake 3.23 or newer.
- A C++20-capable toolchain.
- Ninja when using the checked-in CMake presets.
- Checked-out optional backend dependencies (`git submodule update --init`).
- A C compiler and the native window-system development dependencies needed by
  the copied GLFW source when examples are enabled.

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The checked-in presets build the optional `truffle-shaderc` package assembler
and its pinned offline GLSL/GLSL ES to SPIR-V route. Other consumers can opt in
to the dependency-free assembler with `-DTFL_BUILD_SHADERC=ON`, then enable
the Shader module's glslang adapter explicitly with
`-DTFL_SHADER_ENABLE_GLSLANG=ON`; the runtime RHI never gains a compiler
dependency.
Native Slang modules/imports can independently opt in with
`-DTFL_SHADER_ENABLE_SLANG=ON -DTFL_SHADER_ENABLE_SPIRV_TOOLS=ON`;
see [Slang's pinned source build and bounded target matrix](docs/shader-slang.md).
HLSL compilation is independently enabled with
`-DTFL_SHADER_ENABLE_DXC=ON -DTFL_SHADER_ENABLE_SPIRV_TOOLS=ON`;
it uses pinned in-process DXC for SPIR-V and DXIL, without requiring glslang.
On Windows, source-built DXC also requires the Visual Studio **C++ ATL for
latest v143 build tools (x86 & x64)** component. Activate the developer shell
for an installation containing ATL before configuring/building; an already
active toolchain is preserved by the build scripts.
Portable native outputs additionally require
`-DTFL_SHADER_ENABLE_SPIRV_TOOLS=ON -DTFL_SHADER_ENABLE_SPIRV_CROSS=ON`.
Explicitly prepare their pinned submodules first; configure never downloads.
Native MSL source/reflection is independently enabled with
`-DTFL_SHADER_ENABLE_METAL_COMPILER=ON` on Apple. Offline `metallib` output
additionally needs `-DTFL_SHADER_ENABLE_METALLIB_COMPILER=ON` and the
installed Xcode Metal Toolchain. These flags are disabled by default; prepared
package consumers do not link the compiler.
See the [native MSL and portable Shader target profiles and limits](src/shader/README.md#native-msl-source-and-libraries-188)
and [shaderc examples](docs/rhi1/shader-package.md#tool-usage).

GUI text is enabled by default; prepare its pinned sources with
`git submodule update --init vendor/freetype vendor/harfbuzz vendor/sheenbidi vendor/utf8proc`.
With `-DTFL_GUI_ENABLE_TEXT=OFF`, `gui::TextEngine::available()` is false
and the GUI has no text-library build dependency. FreeType attribution and
license notices are documented in
[`docs/rhi1/dependencies.md`](docs/rhi1/dependencies.md#gui-text).
Binary redistributors must include this acknowledgment in distribution
documentation: "This software is based in part on the work of the FreeType
Team." The enabled install also places it beside the license texts.

Optional WGSL compilation uses pinned in-process Tint with
`-DTFL_SHADER_ENABLE_TINT=ON -DTFL_SHADER_ENABLE_SPIRV_TOOLS=ON`,
independently of glslang. It supports WGSL to SPIR-V 1.3, selected WGSL
regeneration, SPIR-V to WGSL, and optional existing MSL translation. See
[WGSL/Tint routes and truthful language/configuration limits](src/shader/README.md#wgsl-and-tint-191).

`truffle-rhi-doctor --strict --json <path>` records the declared matrix and the
adapters that actually initialized on the current host. Strict mode fails when
a built backend with a `native_smoke` host claim cannot expose its native
adapter. Null is reported separately as validation-only.

See `docs/charter.md`, `docs/architecture.md`, `docs/roadmap.md`, and
`docs/rhi1/` for the current boundaries, replacement sequence, and backend
maturity evidence.

Accepted and deliberately deferred work is tracked in the
[Truffle delivery project](https://github.com/orgs/truffle-graphics/projects/1) and
repository issues. [`docs/planning.md`](docs/planning.md) defines the durable
planning and traceability rules used by contributors and automation.

## Setup and build

On macOS or Debian/Ubuntu Linux:

```sh
./setup.sh
./build.sh
./build.sh release
./build.sh relwithdebinfo
./build.sh debug --warnings-as-errors on --target truffle_rhi_contract_tests --parallel 4
./build.sh debug --warnings-as-errors off --target truffle_rhi_contract_tests
ctest --preset debug -R '^truffle_rhi_contract_tests$'
```

On Windows, from PowerShell:

```powershell
.\setup.ps1
.\build.ps1
.\build.ps1 release
.\build.ps1 debug -WarningsAsErrors on
.\build.ps1 debug -WarningsAsErrors off
ctest --preset debug --output-on-failure
python tools\test_windows_scripts.py
```

Setup checks Git, CMake 3.23+, Ninja and the native C++ toolchain. Missing tools
are installed only after confirmation, using existing Homebrew, apt-get or
winget. Homebrew itself is not bootstrapped. On Windows, setup can install the
Visual Studio 2022 C++ Build Tools and Windows SDK. Setup and build activate the
installed x64 developer environment when necessary; setup discovers its bundled
CMake/Ninja before requesting separate installations. First-party MSVC targets
use UTF-8 source and execution encodings, independent of the system code page.
If an installer needs a new terminal
or Apple's graphical installer needs to finish, setup stops and asks you to rerun.
`setup.sh --check` / `setup.ps1 -Check` perform no installation or fetching.

Setup initializes only the pinned Vulkan headers/volk, glslang and GUI-text
submodules needed by the standard presets. It does not fetch optional DXC, Slang
or Tint toolchains recursively. Dirty submodules are never moved. Additional
shader routes still require their documented dependency preparation.

The scripts work from any current directory. Build configures the selected
preset and forwards build arguments to `cmake --build`. `debug` is Debug,
`release` is optimized, and `relwithdebinfo` is optimized with debugging
information. `package` is an explicit distribution preset, not a warning mode.
Warnings-as-errors is independently toggled with `--warnings-as-errors on|off`
or PowerShell `-WarningsAsErrors on|off`. New builds default to off; each
configuration remembers its setting until explicitly changed. Toggling
reconfigures the same directory, not a separate build tree. Only project-owned
targets adopt this warning policy, not vendored third-party sources.
Direct CMake use remains supported (`-DTFL_WARNINGS_AS_ERRORS=ON` or `OFF`).
Metal defaults on only on Apple hosts.

```text
build/<preset>/        CMake cache, generated build files and intermediate work
out/<preset>/bin/      executables, examples, tests and tools
out/<preset>/lib/      static and shared libraries
install/<preset>/     installed package tree (only after cmake --install)
install/<preset>/packages/  CPack archives and staging
build/_scratch/        temporary validation logs, reports and consumer probes
```

These directories are ignored by Git. No install script is provided yet.
For direct `cmake -S . -B ...` builds, the build-directory basename selects the
output/install name; set `TFL_OUTPUT_DIR` and `CMAKE_INSTALL_PREFIX` to override it.
Explicit `CMAKE_*_OUTPUT_DIRECTORY` settings are respected. Embedded Truffle
does not change its parent's output directories. Multi-configuration generators
append their configuration under `bin` and `lib`; the shipped presets use Ninja.

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

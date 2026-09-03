# RHI 1 Backend And Platform Maturity

Support is reported for a **backend-platform pair**, not for an interface name.
Feature capability and backend maturity are separate: a supported backend may
legitimately report an advanced feature as unsupported.

## Maturity Gates

| State | Required evidence |
|---|---|
| `source_only` | Backend-owned source exists and its target/platform contract is identified. No compile or execution claim is implied. |
| `cross_compiles` | An official CI job compiles and links the native target with its real headers and loader/SDK. A CPU contract simulator does not satisfy this gate. |
| `native_smoke` | CI or recorded hardware evidence discovers a real native adapter/device and submits a trivial native workload with validation enabled. |
| `conformant` | Required shared tests execute natively, including deterministic buffer/texture readback or golden output, synchronization/error paths, and capability-accurate unsupported results. |
| `supported` | Conformance is repeatable on the declared platform window; native presentation is proven; validation is clean; packaging, prerequisites, recovery behavior, and known limits are documented. |

Promotion requires all earlier evidence and linked CI or hardware records. A
regression can demote one pair without changing another backend or platform.

## Current Repository Evidence

| Implementation | Current platform | Kind | Current state | Evidence and limitation |
|---|---|---|---|---|
| Null validation | All CI hosts | Strict RHI 1 contract interpreter | validation-only | Generation, lifetime, state, threading, resource, synchronization, acquire, submit, and present tests; never reported as a GPU backend. |
| Metal | macOS | Native Metal resource, pipeline, synchronization, and presentation slice | `native_smoke` | Metal API validation runs deterministic output, ordered render-to-copy, shared-event synchronization, native `CAMetalLayer` acquire/render/present plus resize/status recovery, and private out-of-date/device-loss fault proofs. A host-window loop, physical device-removal evidence, broader platform execution, and full conformance remain. |
| Vulkan | Linux | Native resource and transfer smoke slice | `native_smoke` | Pinned private Vulkan-Headers/volk load the host runtime and optionally enable Khronos validation. Native buffers, selected device-local 2D textures/views, buffer copies/fills, padded-row buffer-texture copies, and texture copies pass exact readback. Texture clear/resolve/blit, compressed and multisampled textures, pipelines, synchronization, WSI, presentation, and full conformance remain unsupported. |
| Vulkan | Windows, Android, macOS/iOS | Native loader source with platform targets | `source_only` | No official native runtime lanes yet. Portability enumeration/subset handling is present, but MoltenVK is not bundled and no Apple claim is made. |
| Direct3D 12 | Windows | Native Windows-SDK WARP resource and pipeline smoke slice | `native_smoke` | Creates the DXGI WARP adapter and owns committed buffers, selected device-local textures and views, transfers, ShaderPackage-backed root signatures, graphics/compute PSOs, and draw/dispatch commands. Debug-layer tests check exact transfer, triangle, binding, depth/MRT/MSAA, indirect, and compute-to-render output. Compressed/external textures, explicit texture clear/resolve/blit, synchronization, DXGI presentation, and full conformance remain unsupported. |
| OpenGL | Linux | Native surfaceless EGL OpenGL buffer and transfer smoke slice | `native_smoke` | Creates a real context and pbuffer, then owns mapped/device-local buffers and views with native copies/fills and exact readback. Textures, pipelines, broader state tracking, window presentation, and full conformance remain unsupported. |
| OpenGL | Windows, macOS | Source target | `source_only` | WGL and deprecated macOS native execution lanes are not present. |
| OpenGL ES | Linux | Native surfaceless EGL ES 3 buffer and transfer smoke slice | `native_smoke` | Creates a real ES context and pbuffer, then owns mapped/device-local buffers and views with native copies/fills and exact readback. Textures, pipelines, Android surfaces, presentation, and full conformance remain unsupported. |
| OpenGL ES | Android | Source target | `source_only` | No NDK/emulator execution lane yet. |
| WebGPU | Web | Explicit unavailable factory | `source_only` | Target exists, but pinned Emdawnwebgpu and browser device execution are not present; no adapter is exposed. |
| WebGL2 | Web | Browser context source path | `source_only` | The Emscripten path creates a WebGL2 canvas context only at runtime; no browser CI evidence exists, so no promotion is claimed. |

The old named-backend CPU simulators were removed during the RHI 1 public
cutover. Shared logical behavior belongs to Null. A native matrix adapter has
an intentionally empty capability surface until backend-owned RHI mechanisms
exist; unsupported work never falls back to CPU simulation.

The public `backend_platform_support()` table serializes these rows, including
separate compile, smoke, conformance, validation, and presentation evidence.
`truffle-rhi-doctor` adds current-host build/runtime probes. Strict CI fails if
a linked `native_smoke` host row cannot initialize its native adapter.

## Phase 5 Native Slice Evidence

The following receipts are the source of truth used for the native claims
above. Maturity remains `native_smoke`: these slices prove real native objects
and deterministic output, but do not satisfy the complete conformance or
presentation gates.

| Slice | Merged source | Native CI evidence | Proven result |
|---|---|---|---|
| Vulkan buffers | [`cc19d647`](https://github.com/truffle-graphics/truffle/commit/cc19d6474d0425885e42264ec03fd91bca617a62) via [PR #44](https://github.com/truffle-graphics/truffle/pull/44) | [Build `33260608283`](https://github.com/truffle-graphics/truffle/actions/runs/33260608283) Ubuntu validation lane | Upload/readback/device-local buffers, views, native copies and fills, exact mapped readback. |
| Vulkan textures | [`8b0a988e`](https://github.com/truffle-graphics/truffle/commit/8b0a988e2cdeb23d91bd7a6aa775676e1e948d5d) via [PR #45](https://github.com/truffle-graphics/truffle/pull/45) | [Build `33261166883`](https://github.com/truffle-graphics/truffle/actions/runs/33261166883) Ubuntu validation lane | Selected device-local 2D textures/views and an exact padded-row buffer -> texture -> texture -> buffer round trip. |
| D3D12 buffers | [`b983aec4`](https://github.com/truffle-graphics/truffle/commit/b983aec4eca521620a44552da04a8ad5ee1f52ab) via [PR #46](https://github.com/truffle-graphics/truffle/pull/46) | [Build `33261487148`](https://github.com/truffle-graphics/truffle/actions/runs/33261487148) Windows debug-layer lane | Upload/readback/default-heap buffers, views, native copies and arbitrary-range fills with exact WARP readback. |
| D3D12 textures | [`d793f34`](https://github.com/truffle-graphics/truffle/commit/d793f3493a14a277a38cbd40f3ef3b64b84fe67a) via [PR #135](https://github.com/truffle-graphics/truffle/pull/135) | [Build `33759484321`](https://github.com/truffle-graphics/truffle/actions/runs/33759484321) Windows debug-layer lane | Device-local uncompressed textures, native SRV/UAV/RTV/DSV descriptors, per-subresource transitions, and an exact 256-byte-padded WARP buffer -> texture -> texture -> buffer round trip. |
| D3D12 bindings and pipelines | [`0f7a4f1`](https://github.com/truffle-graphics/truffle/commit/0f7a4f13cd35feedf7d03490540546383815b6b9) via [PR #136](https://github.com/truffle-graphics/truffle/pull/136) | [Build `33762763659`](https://github.com/truffle-graphics/truffle/actions/runs/33762763659) Windows debug-layer lane | Native HLSL/DXIL ownership, reflected root signatures, descriptor arrays, immutable samplers, dynamic offsets, push constants, graphics/compute PSOs, and exact triangle, textured, depth/MRT/MSAA, indexed/instanced/indirect, and compute-to-render output. |
| OpenGL and OpenGL ES buffers | [`df019385`](https://github.com/truffle-graphics/truffle/commit/df01938528a06f11d156c5760e255c69db3f83cb) via [PR #47](https://github.com/truffle-graphics/truffle/pull/47) | [Build `33262121061`](https://github.com/truffle-graphics/truffle/actions/runs/33262121061) Ubuntu EGL validation lanes | Shared desktop GL/ES upload/readback/device-local buffers, views, copies and arbitrary-range fills with exact native readback. |

## Target Matrix

These are implementation targets, not present support claims.

| Backend | Target platforms | Position |
|---|---|---|
| Null validation | All | Strict contract interpreter and negative-path oracle. |
| Metal | macOS 13+; iOS/iPadOS/tvOS 16+; visionOS 1+ | Primary Apple implementation. |
| Vulkan | Windows 10+; Ubuntu 22.04-class Linux; Android API 26+; Apple through MoltenVK | Primary portable explicit implementation. |
| Direct3D 12 | Windows 10 SDK 19041+, x64 and arm64 where available | Primary Windows implementation, with WARP validation. |
| WebGPU | Evergreen wasm32 browsers through Emdawnwebgpu; optional Dawn-native tests | Primary modern web implementation. |
| OpenGL | Windows/Linux 4.5 core; macOS 4.1 experimental | Selected desktop compatibility implementation. |
| OpenGL ES | Android/Linux EGL, ES 3.1 | Mobile and embedded compatibility implementation. |
| WebGL2 | Evergreen wasm32 browsers | Capability-limited browser fallback. |

Vulkan on Apple is a portability path rather than native Vulkan. OpenGL on
macOS remains experimental. D3D11, WebGL1, older GL/GLES profiles, CUDA,
OpenCL, and public proprietary-console implementations are deferred.

## Conformance Evidence

Required native suites grow by capability slice and include:

- adapter discovery and required/optional feature negotiation;
- buffer upload/readback and texture subresource round trips;
- clear, triangle, indexed/instanced, depth/stencil, MRT, MSAA resolve,
  textured, and compute-to-render output tests;
- descriptor arrays, dynamic offsets, bindless tables, and layout mismatch
  tests where supported;
- multi-queue semaphore, fence, barrier, and ownership-transfer tests;
- resize, `out_of_date`, surface loss, device loss, and presentation recovery;
- shader-package reflection equivalence across selected target variants;
- capability-gated advanced-feature output tests.

Successful command submission without native output evidence is insufficient.

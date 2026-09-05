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
| Vulkan | Linux | Native resource, pipeline, synchronization, and XCB presentation smoke slice | `native_smoke` | Pinned private Vulkan-Headers/volk load the host runtime and optionally enable Khronos validation. Native buffers and capability-checked images support views, subresource copies, clear, resolve, and blit with exact readback. Generated ShaderPackage SPIR-V executes through descriptor-backed sampled-texture/sampler and storage-buffer bindings, direct and indirect compute, plus graphics pipelines with exact direct/indexed/indirect, depth-tested, MRT, and four-sample-resolved output. Discovered graphics/compute/transfer queues, timeline semaphores, explicit barriers, fences, and timestamp/occlusion queries have ordered native evidence. Host-owned XCB surfaces acquire, render, read back, present, and recreate after resize under Xvfb; other Linux surface kinds remain unsupported. External sharing, indirect-count, and full conformance remain unsupported. |
| Vulkan | Windows, Android, macOS/iOS | Native loader source with platform targets | `source_only` | No official native runtime lanes yet. Portability enumeration/subset handling is present, but MoltenVK is not bundled and no Apple claim is made. |
| Direct3D 12 | Windows | Native Windows-SDK WARP resource, pipeline, synchronization, and presentation smoke slice | `native_smoke` | Creates the DXGI WARP adapter and owns resources/transfers, ShaderPackage-backed pipelines, barriers, timeline fences, timestamp/occlusion queries, and host-owned Win32/DXGI flip-model presentation. Debug-layer and GPU validation check exact output/readback, ordered work, resize, and typed recovery. Compressed/external textures, explicit texture clear/resolve/blit, native multi-queue ownership transfer, non-Win32 surfaces, mailbox presentation, and full conformance remain unsupported. |
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
and deterministic output but not complete conformance. Presentation is claimed
only by rows with a native presentation receipt.

| Slice | Merged source | Native CI evidence | Proven result |
|---|---|---|---|
| Vulkan buffers | [`cc19d647`](https://github.com/truffle-graphics/truffle/commit/cc19d6474d0425885e42264ec03fd91bca617a62) via [PR #44](https://github.com/truffle-graphics/truffle/pull/44) | [Build `33260608283`](https://github.com/truffle-graphics/truffle/actions/runs/33260608283) Ubuntu validation lane | Upload/readback/device-local buffers, views, native copies and fills, exact mapped readback. |
| Vulkan textures | [`8b0a988e`](https://github.com/truffle-graphics/truffle/commit/8b0a988e2cdeb23d91bd7a6aa775676e1e948d5d) via [PR #45](https://github.com/truffle-graphics/truffle/pull/45) | [Build `33261166883`](https://github.com/truffle-graphics/truffle/actions/runs/33261166883) Ubuntu validation lane | Selected device-local 2D textures/views and an exact padded-row buffer -> texture -> texture -> buffer round trip. |
| Vulkan resource expansion | [`f06050c`](https://github.com/truffle-graphics/truffle/commit/f06050c) via [PR #140](https://github.com/truffle-graphics/truffle/pull/140) | [Build `33817112365`](https://github.com/truffle-graphics/truffle/actions/runs/33817112365) Ubuntu validation lane | Required 1D/3D/cube/array/mip, BC1, depth/stencil clear/readback, four-sample resolve, and linear-blit proofs plus exact subresource output. |
| Vulkan host-visible textures | [`8be416a`](https://github.com/truffle-graphics/truffle/commit/8be416a) via [PR #141](https://github.com/truffle-graphics/truffle/pull/141) | [Build `33817918711`](https://github.com/truffle-graphics/truffle/actions/runs/33817918711) Ubuntu validation lane | Runtime-queried linear upload/readback images, native row/depth pitches, explicit host/queue synchronization and coherence, padded exact output, and logical budget retirement. |
| Vulkan resource audit | [`0c8ec99`](https://github.com/truffle-graphics/truffle/commit/0c8ec99) via [PR #142](https://github.com/truffle-graphics/truffle/pull/142) | [Build `33818770807`](https://github.com/truffle-graphics/truffle/actions/runs/33818770807) Ubuntu validation lane | Overflow-safe host layout bounds plus deterministic native rejection of invalid cube views, host-visible multisampling, partial clears, sampler access, external sharing, and budget exhaustion. |
| Vulkan shader modules | [`fee0d9b`](https://github.com/truffle-graphics/truffle/commit/fee0d9be76201bd4c4dec02eb3a66ebce217f32d) via [PR #144](https://github.com/truffle-graphics/truffle/pull/144) | [Build `33823883160`](https://github.com/truffle-graphics/truffle/actions/runs/33823883160) Ubuntu validation lane | A generated glslang SPIR-V package creates an aligned, owned native shader module; non-SPIR-V, partial-word, and invalid-magic inputs fail deterministically. Binding and pipeline capabilities remain unadvertised. |
| Vulkan bindings and compute | [`4aff74e`](https://github.com/truffle-graphics/truffle/commit/4aff74e) via [PR #145](https://github.com/truffle-graphics/truffle/pull/145) | [Build `33939221825`](https://github.com/truffle-graphics/truffle/actions/runs/33939221825) Ubuntu validation lane | Native samplers, descriptor-set/pipeline layouts, immutable groups and arrays, dynamic offsets, push constants, specialization, and direct compute dispatch; generated SPIR-V writes four exact storage-buffer words. Graphics and indirect paths remain gated. |
| Vulkan direct graphics | [`b520402`](https://github.com/truffle-graphics/truffle/commit/b520402) via [PR #146](https://github.com/truffle-graphics/truffle/pull/146) | [Build `33940219142`](https://github.com/truffle-graphics/truffle/actions/runs/33940219142) Ubuntu validation lane | Generated vertex/fragment SPIR-V creates an owned compatible render pass and graphics pipeline; native clear, direct triangle draw, attachment transition, and copy produce an exact red center pixel. Depth/MRT/MSAA and indirect paths remain gated. |
| Vulkan expanded graphics | [`eaee5b8`](https://github.com/truffle-graphics/truffle/commit/eaee5b8) via [PR #147](https://github.com/truffle-graphics/truffle/pull/147) | [Build `33941005375`](https://github.com/truffle-graphics/truffle/actions/runs/33941005375) Ubuntu validation lane | Reflected sampled-texture/sampler descriptors produce an exact green triangle; indexed, instanced, and indirect draws produce exact red output; dispatch-indirect reproduces the direct storage-buffer result. Depth/MRT/MSAA and indirect-count remain gated. |
| Vulkan depth, MRT, and MSAA | [`6cfc3b8`](https://github.com/truffle-graphics/truffle/commit/6cfc3b8) via [PR #148](https://github.com/truffle-graphics/truffle/pull/148) | [Build `33942299645`](https://github.com/truffle-graphics/truffle/actions/runs/33942299645) Ubuntu validation lane | Depth comparison produces exact pass/fail pixels, a generated two-output fragment shader produces exact red/green targets, and four-sample rendering resolves to exact red output. Descriptor images transition before render-pass execution, keeping validation clean. |
| Vulkan binding/pipeline closeout | [`4dd7730`](https://github.com/truffle-graphics/truffle/commit/4dd7730) via [PR #149](https://github.com/truffle-graphics/truffle/pull/149) | [Build `33943018863`](https://github.com/truffle-graphics/truffle/actions/runs/33943018863) Ubuntu validation lane | Compute-to-render ordering produces an exact storage-driven blue pixel; recorded groups survive arena reset, retired groups fail, and replacements execute. Reflection/layout mismatches plus unadvertised remap, bindless, update-after-bind, cache, and indirect-count paths fail deterministically. |
| Vulkan synchronization and queries | [`01c5bac`](https://github.com/truffle-graphics/truffle/commit/01c5bac) via [PR #150](https://github.com/truffle-graphics/truffle/pull/150) | [Build `33944555436`](https://github.com/truffle-graphics/truffle/actions/runs/33944555436) Ubuntu validation lane | Discovered graphics/compute/transfer queues, concurrent cross-family resource sharing, native timeline waits/signals, multi-list ordering, buffer/texture/aliasing barriers, deterministic fence and semaphore timeout/retry, timestamp ordering, and bounded nonzero occlusion readback. |
| Vulkan synchronization recovery and calibration | [`36585e8`](https://github.com/truffle-graphics/truffle/commit/36585e8) via [PR #151](https://github.com/truffle-graphics/truffle/pull/151) | [Build `33945096780`](https://github.com/truffle-graphics/truffle/actions/runs/33945096780) Ubuntu validation lane | Private fault injection proves deterministic Vulkan device-loss propagation and fresh-device recovery; Vulkan and D3D12 publish native nanoseconds-per-timestamp-tick calibration and require a positive value whenever timestamp queries are advertised. |
| Vulkan Linux XCB presentation | [`dc67b1d`](https://github.com/truffle-graphics/truffle/commit/dc67b1d) via [PR #152](https://github.com/truffle-graphics/truffle/pull/152) | [Build `33961381232`](https://github.com/truffle-graphics/truffle/actions/runs/33961381232) Ubuntu validation and Xvfb presentation lanes | Host-owned XCB surface and native swapchain creation, synchronous fence-backed acquire, timeline-to-binary present bridging, exact padded BGRA8 readback, FIFO presentation, host-window resize, and native swapchain recreation. Unsupported surface kinds fail explicitly; private hooks cover typed nondeterministic WSI failures without simulating successful presentation. |
| Offline GLSL compiler route | [`122b15b`](https://github.com/truffle-graphics/truffle/commit/122b15b) via [PR #143](https://github.com/truffle-graphics/truffle/pull/143) | [Build `33819959268`](https://github.com/truffle-graphics/truffle/actions/runs/33819959268) package, macOS, Ubuntu, and Windows lanes | Pinned glslang 16.5.0 compiles GLSL/GLSL ES to deterministic SPIR-V packages with exact provenance and bounded invalid-source diagnostics; compiler-disabled package isolation also passes. |
| D3D12 buffers | [`b983aec4`](https://github.com/truffle-graphics/truffle/commit/b983aec4eca521620a44552da04a8ad5ee1f52ab) via [PR #46](https://github.com/truffle-graphics/truffle/pull/46) | [Build `33261487148`](https://github.com/truffle-graphics/truffle/actions/runs/33261487148) Windows debug-layer lane | Upload/readback/default-heap buffers, views, native copies and arbitrary-range fills with exact WARP readback. |
| D3D12 textures | [`d793f34`](https://github.com/truffle-graphics/truffle/commit/d793f3493a14a277a38cbd40f3ef3b64b84fe67a) via [PR #135](https://github.com/truffle-graphics/truffle/pull/135) | [Build `33759484321`](https://github.com/truffle-graphics/truffle/actions/runs/33759484321) Windows debug-layer lane | Device-local uncompressed textures, native SRV/UAV/RTV/DSV descriptors, per-subresource transitions, and an exact 256-byte-padded WARP buffer -> texture -> texture -> buffer round trip. |
| D3D12 bindings and pipelines | [`054d111`](https://github.com/truffle-graphics/truffle/commit/054d111cfd7535b24579ac84d16c30993a92ace0) via [PR #136](https://github.com/truffle-graphics/truffle/pull/136) | [Build `33763713016`](https://github.com/truffle-graphics/truffle/actions/runs/33763713016) Windows debug-layer lane | Native HLSL/DXIL ownership, reflected root signatures, descriptor arrays, immutable samplers, dynamic offsets, push constants, graphics/compute PSOs, and exact triangle, vertex-input/blend, textured, depth/MRT/MSAA, indexed/instanced/indirect, and compute-to-render output. |
| D3D12 synchronization and queries | [`e6e75c3`](https://github.com/truffle-graphics/truffle/commit/e6e75c35e525b291ab448bce945d73210d34a142) via [PR #137](https://github.com/truffle-graphics/truffle/pull/137) | [Build `33766961283`](https://github.com/truffle-graphics/truffle/actions/runs/33766961283) Windows debug-layer and GPU-validation lane | Native timeline-fence waits/signals, multi-list ordering, legacy transitions/UAV/aliasing barriers, deterministic timeout/retry, timestamp ordering, exact 64-sample occlusion readback, and typed misuse failures. |
| D3D12 Win32/DXGI presentation | [`df388c0`](https://github.com/truffle-graphics/truffle/commit/df388c07c76ffeb10e91eb36e12fd3d7a1c4a108) via [PR #138](https://github.com/truffle-graphics/truffle/pull/138) | [Build `33815232612`](https://github.com/truffle-graphics/truffle/actions/runs/33815232612) Windows presentation and GPU-validation lane | Host-owned Win32 window, flip-model acquire/render/present, exact BGRA8 back-buffer readback, semaphore ordering, client-size drift, aligned resize/recreation, occlusion, typed recovery statuses, and destroyed-window/device-loss paths. |
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

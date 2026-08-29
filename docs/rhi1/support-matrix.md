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
| Metal | macOS | Native Metal resource, pipeline, synchronization, and presentation slice | `native_smoke` | Metal API validation runs deterministic output, ordered render-to-copy, shared-event synchronization, native `CAMetalLayer` acquire/render/present plus resize/status recovery, and private device-loss fault/recreation proofs. A host-window loop, physical device-removal evidence, broader platform execution, and full conformance remain. |
| Vulkan | Declared targets only | Explicitly unavailable | `source_only` | The factory returns `unsupported` and reports no adapter; no Vulkan headers, loader, device, or WSI are used yet. |
| Direct3D | Declared targets only | Explicitly unavailable | `source_only` | The factory returns `unsupported` and reports no adapter; no D3D12/DXGI APIs or WARP execution exist yet. |
| OpenGL | Declared targets only | Explicitly unavailable | `source_only` | The factory returns `unsupported` and reports no adapter; no GL loader, native context, or driver execution exists yet. |
| WebGPU | None | Not implemented | planned | No backend target exists. |
| OpenGL ES | None | Not implemented | planned | No EGL/GLES backend target exists. |
| WebGL2 | None | Not implemented | planned | No Emscripten WebGL2 backend target exists. |

The old named-backend CPU simulators were removed during the RHI 1 public
cutover. Shared logical behavior belongs to Null; named factories remain
unavailable until they have native implementations.

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

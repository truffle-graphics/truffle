# Consumer Layer Audit And Contracts (#273)

The approved model is three independently usable levels: GUI owns GUI graphics
orchestration, Renderer owns normal graphics resource/rendering mechanisms, and
RHI is an explicit specialist API. Native windows/event loops/input/platform
services remain host-owned. Engine design #272 is excluded.

## Audit at #271

All 15 Renderer and 23 GUI public headers and their CMake targets were audited.
The table inventories every RHI-facing family, including private storage; other
headers have no RHI requirement. **Equivalent** means a normal consumer needs a
layer-owned replacement. **Specialist** means explicitly low-level use.
**Fine** means implementation-only storage or a layer-neutral contract.

| Header / surface | Exposure | Classification and disposition |
|---|---|---|
| `renderer/presentation.hpp` | `NativeSurface`, resize/frame `Extent2D`, `TextureFormat`, `PresentMode` | Equivalent: shared host-window/pixel values (#274), Renderer presentation values (#275) |
| Same | Device/Instance/DeviceDesc creation, device/swapchain accessor, BackendKind/InstanceDesc/DeviceDesc options | Equivalent for normal setup/device sharing; raw instance/features/swapchain/diagnostics remain Specialist (#275) |
| `renderer/renderer.hpp` | Device constructor/accessor, Swapchain render argument, Fence signal | Equivalent: Renderer device, presentation and pacing; direct external fence/swapchain submission is Specialist (#275) |
| `renderer/mesh.hpp` | Device upload, IndexFormat, private Buffer storage | Equivalent device/index values; private GPU storage Fine (#275) |
| `renderer/render_batch.hpp` | Buffer pointers/ranges, BindGroup pointers/dynamic offsets, IndexFormat, ScissorRect | Equivalent: owned Renderer buffer slices/bindings/index/clip values (#275); direct arenas/descriptor mutation Specialist |
| `renderer/frame_stream.hpp` | Device/pacing Fence, BufferUsage, Buffer slots | Equivalent safe-slot/stream contracts (#275); explicit external fences Specialist; private storage Fine |
| `renderer/texture.hpp` | Device, image extents/formats/Origin3D, Texture/View, attachment/sample TextureBarrier | Equivalent images/targets and semantic transitions (#275); raw subresources/queue barriers Specialist |
| Same | ShaderStageMask, BindGroupLayout/PipelineLayout, SamplerDesc, BindGroup/Sampler/DescriptorArena | Equivalent normal sampled bindings/samplers (#275); arbitrary descriptor arenas/raw layouts Specialist |
| `renderer/depth_target.hpp` | Device/Extent2D/TextureFormat/Texture | Equivalent depth target/format/attachment (#275); private storage Fine |
| `renderer/pipeline_cache.hpp` | ShaderPackage/Target/Stage selection, PipelineDesc, vertex/color/depth/raster/sample state, Pipeline return | Equivalent prepared programs and Renderer graphics/vertex state (#275); target chosen from device |
| Same | Raw ShaderBinding, layout identity, native staging/reflection, IPipelineCache override | Specialist native pipeline/compiler/cache extension; private pipeline/storage Fine |
| `renderer/data_layout.hpp` | VertexBufferLayoutDesc, ShaderInterfaceVariable | Equivalent Renderer vertex declarations; raw reflection validator Specialist (#275) |
| `renderer/source_program.hpp` | CompileRequest stages, raw PipelineDesc | Specialist optional Shader compilation/rebuild route; normal prepared program path must not require it |
| `renderer/transform_compute_pass.hpp` | Device/Shader/CommandList/ComputePipeline/Buffer pointers | Specialist explicit raw transform compute and command recording; no new general compute/scene framework |
| `renderer/view.hpp`, `components.hpp`, `shaders.hpp` | Camera math/components/source text | Fine; no RHI setup or type required |
| `gui/draw.hpp` | target Extent2D, clip ScissorRect | Equivalent GUI pixel geometry (#276); CPU assembly needs no Renderer/RHI |
| `gui/painter.hpp` | Device/TextureFormat/SamplerDesc/Extent2D plus Renderer cache/texture/batches | Equivalent for ordinary GUI via Application/image APIs (#276); explicit raw Painter becomes Specialist |
| `gui/text.hpp` | Device creation, atlas Extent2D and Renderer texture exposure | Equivalent font/text creation owned by GUI Application (#276); explicit atlas GPU access Specialist |
| `gui/surface.hpp` | Painter record with RHI extent | Equivalent advanced GUI rendering integration; raw record Specialist (#276/#277) |
| `gui/viewport.hpp` | RHI pixels/format/device/pass plus Renderer batches/graph | Equivalent GUI metrics and opt-in Renderer-level viewport composition (#277) |
| Other GUI headers (Context, geometry, elements, layout, shapes, events/input/host, widgets/themes, builder/regions/collections, menus, text edit, docking) | CPU/layout/input/state | Fine; preserve transparent roots and host services |
| Native examples/tests | Instance/adapter/device, native window/swapchain, explicit command/fence, GPU readback | Specialist proofs. Readback is test evidence, not a GUI/Renderer runtime requirement |
| GUI-only/normal Renderer examples | Repeated native setup and raw types | Replace with layer-owned examples; explicit native proofs opt into specialist headers |

## CMake audit

`Truffle::Renderer` currently publicly links Shader/RHI contracts but no
backend. Its optional `Truffle::RendererPresentation` links only enabled
Metal/Vulkan implementations. `Truffle::GUI` links Renderer and private optional
pinned text runtimes; it currently has no owned-device entry point.
`Truffle::GUIDock` remains above GUI. Transitive static implementation linkage
does not mean consumers must include/name those lower-layer contracts.

The cutover retains this direction. Native setup stays optional; GUI exposes
owned setup when that part is enabled and explicitly reports unsupported when
disabled. Consumers link GUI alone for GUI-owned setup, or opt into Renderer
when sharing/rendering. No mandatory backend, shader compiler or Engine link
is introduced into core Renderer.

## Delivery and public boundaries

Focused structural children of #273: host-window values (#274), normal Renderer
resource/frame contracts (#275), owned/borrowed GUI entry points (#276), and
shared Renderer-level GUI viewport integration (#277). Existing RHI-facing
contracts move to explicit specialist paths/namespaces; no old-name aliases,
forwarding shims or version bump. Camera/component values remain ordinary.

`WindowHandle`, `WindowKind`, `PixelSize` and `PixelRect` are canonical Base
values re-exported by GUI/Renderer, not aliases of RHI. HWND uses `handle` and
optional HINSTANCE `display`; CAMetalLayer is supplied directly, not an inferred
NSWindow/NSView conversion. Existing XCB and unsupported-kind conventions remain.

Verification is limited to affected incremental compilation/focused contracts.
Native execution, multi-window integration, uncompiled platforms, full suites
and package/install/hosted matrices are owner-run, not inferred from API work.

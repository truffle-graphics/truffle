# Consumer Layer Audit And Contracts (#273)

The approved model is three independently usable levels: GUI owns GUI graphics
orchestration, Renderer owns normal graphics resource/rendering mechanisms, and
RHI is an explicit specialist API. Native windows/event loops/input/platform
services remain host-owned. Engine design #272 is excluded.

## Audit at #271

All 15 Renderer and 23 GUI public headers and their CMake targets were audited:
145 RHI references in 17 headers, including private storage.
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
| `renderer/frame_graph.hpp` | RenderPassDesc/attachments/texture pointers, extents, clear colors, load/store/depth/resolve and TextureBarrier recording | Equivalent typed passes, retained targets and semantic transitions (#275); raw nodes/encoder access Specialist |
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
| `gui/painter.hpp` | Device/TextureFormat/SamplerDesc/Extent2D plus Renderer cache/texture/batches | Equivalent for ordinary GUI via Graphics/image APIs (#279); explicit raw Painter becomes Specialist |
| `gui/text.hpp` | Device creation, atlas Extent2D and Renderer texture exposure | Equivalent font/text creation owned by GUI Graphics (#279); explicit atlas GPU access Specialist |
| `gui/surface.hpp` | Painter record with RHI extent | Equivalent advanced GUI rendering integration; raw record Specialist (#276/#277) |
| `gui/viewport.hpp` | RHI pixels/format/device/pass plus Renderer batches/graph | Equivalent GUI metrics and opt-in Renderer-level viewport composition (#277) |
| Other GUI headers (Context, geometry, elements, layout, shapes, events/input/host, widgets/themes, builder/regions/collections, menus, text edit, docking) | CPU/layout/input/state | Fine; preserve transparent roots and host services |
| Native examples/tests | Instance/adapter/device, native window/swapchain, explicit command/fence, GPU readback | Specialist proofs. Readback is test evidence, not a GUI/Renderer runtime requirement |
| GUI-only/normal Renderer examples | Repeated native setup and raw types | Replace with layer-owned examples; explicit native proofs opt into specialist headers |

## CMake audit and delivered disposition

At #271, `Truffle::Renderer` publicly linked Shader/RHI contracts but no
backend. Its optional `Truffle::RendererPresentation` links only enabled
Metal/Vulkan implementations. `Truffle::GUI` links Renderer and private optional
pinned text runtimes; its optional graphics-device entry point is now
`gui::Graphics`, with optional native factory linkage private to GUI.
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

## Renderer contracts (#275)

Ordinary Renderer headers contain independent graphics values and retained
resources: `Device`, `Buffer`/`BufferSlice`, `Mesh`, `SampledTexture`/`Texture`,
`DepthTarget`, `ProgramLayout`/`ResourceBindings`, `ShaderPackage`,
`PipelineCache`, `FrameGraph`, `Renderer`, `FramePacer`, `StreamBuffer` and
`Presentation`. They neither include nor name RHI; the architecture guard
enforces this surface. Normal passes own texture references and batches own
buffer/binding references. The graph retains those wrappers through recording.
Streamed slots still require the pacing wait; never resubmit a stale graph
after reusing its slot. Program publication retains packages/layouts and
Renderer retains its cache state.

Load prepared `.tsp` bytes with `renderer::ShaderPackage::load`, describe
vertex/graphics state with Renderer values and register a `ProgramDesc`.
The actual device selects the prepared target; absent/incompatible variants
fail rather than triggering runtime compilation or changing backend.
Application-defined mesh/buffer packing remains unchanged. `Mesh::batch`
returns a checked retained batch. `TextureTransition` expresses attachment/
sample use instead of exposing raw barriers. GUI/scene passes can inhabit
the same `FrameGraph`.

Native owned setup uses `Device::create` or `create_native_presentation` and
links the optional `Truffle::RendererPresentation`. Borrowed setup uses
`Presentation::create(Device&, desc)`. Specialists can explicitly include
`renderer/rhi_interop.hpp` to import an existing raw device or resource and
access the native implementations under `renderer::specialist` and
`renderer/specialist/`. Borrowed raw objects stay alive and unmoved; imports
without a supplied lifetime owner do not acquire ownership. These are opt-in
extension contracts, not compatibility shims.

`examples/renderer_consumer/frame.hpp` demonstrates a normal window frame.
Existing explicit-native examples and readback proofs now name specialist
contracts. The focused Null contract covers retained mesh/buffer/texture/
bindings, program publication, semantic transitions, pacing, offscreen
rendering, borrowed identity and owned storage. It is not GPU evidence.
The optional EnCom UploadRing adapter and raw diagnostics remain specialist
integration paths; they are not prerequisites for normal Renderer consumption.

## GUI entry points (#276)

`gui::Graphics::create(GraphicsDesc)` is the GUI-only owned graphics path.
`GraphicsDesc` uses GUI native-window/pixel/DPI values; consumers include GUI
and link GUI alone. Logical state lives in an independently owned
`gui::Context`, which is passed explicitly to graphics operations. Image
upload and font/atlas creation belong to Graphics. Normal Context/draw/text/
Surface contracts have no Renderer/RHI import.
The explicitly optional `gui::Rendering` surface accepts a Renderer Device and
exposes its shared cache/Renderer/Presentation and GUI frame composition.
It contains Renderer values only, not RHI. Raw Painter/text/Surface GPU access
is under `gui/specialist/`. See [Graphics usage](gui-graphics.md).
Native factories travel privately through GUI only when enabled; core Renderer
still has no backend implementation link.

## Viewport boundary (#277)

Ordinary `gui/viewport.hpp` now contains GUI-only `Viewport`, pixel/DPI metrics
and routed events. `gui/rendering.hpp` contains Renderer-only target/frame
values and prepare/append/display/composite/finish/release operations.
It reuses the existing offscreen mechanism under `gui::specialist::Viewport`.
No ordinary GUI header imports Renderer/RHI; the opt-in Rendering header
imports Renderer but no RHI/specialist contract. The guard checks both rules.

Graphics preparation infers matching safe slots. Returned target textures
pin their exact allocation and device owner through resize/release; inserted
passes retain normal batches/depth/textures and GUI owners. Existing clipping,
input, detached DPI, scene revision, submission-vs-presentation, transitions
and transparent roots remain. See [viewport contracts](gui-viewport.md).

The GUI-only direct-link proof, normal viewport contract and migrated deep
specialist viewport contract passed on CPU/Null. The native Vulkan viewport
target compiled after migrating its shared-frame body to normal APIs.
Native initialization/readback alone remain specialist test harness code;
native execution/Metal/configuration/multi-window evidence was not run.
The normal shared-context, independently paced multi-window ownership API is
not supplied by this single-Graphics cutover; #279 records that design
boundary. Existing per-window raw Painter ownership remains specialist.

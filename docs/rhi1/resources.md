# RHI 1 Resources, Memory, Views, And Transfers

Issue #29 establishes the resource slice used by later binding, pipeline, and
synchronization work. Resource operations either execute through the selected
backend or return a typed unsupported result; named backends never fall back to
Null storage.

## Buffers And Textures

Buffers declare a non-zero size, bitmask usage, memory domain, optional initial
mapping, and optional shareability. Textures declare 1D, 2D, 3D, or cube shape;
array layers; mip levels; sample count; color, sRGB, compressed, depth, or
depth/stencil format; usage; memory domain; and optional shareability.

Validation enforces dimension-specific rules: 1D textures have unit height and
depth, 3D textures have one array layer, cube layers are a multiple of six, and
multisampled textures have one mip level. Compressed region origins are block
aligned, with partial blocks allowed only at a mip edge.

`BufferView` owns a checked byte range and optional stride. `TextureView` owns a
checked aspect/mip/layer range and permits only compatible linear/sRGB format
reinterpretation. Views retain their resource payload after the original public
resource wrapper is destroyed.

## Memory And Coherence

- `upload` and `readback` memory are host visible. Mapping, direct writes or
  reads, flush, and invalidate are explicit.
- `device_local` memory is not host mappable. Data reaches it through transfer
  commands.
- `external` memory is accepted only through import calls on a backend that
  reports external-memory capability.
- `MemoryBudget` reports budget and current committed bytes per domain.
- Device-scoped allocator callbacks can admit or reject each allocation and are
  called again when the final retained payload retires. Their callback and user
  data lifetime must cover the device and every resource created from it.

An allocator rejection or budget exhaustion returns `out_of_memory`. Recorded
commands and views retain resource payloads, so budget usage is released only
after the last owner and submitted work release the resource.

## Transfer Contract

A copy encoder records buffer copy/fill, buffer-to-texture,
texture-to-buffer, texture copy, texture clear, resolve, and texture blit.
Usages, bounds, subresources, formats, sample counts, row layout, and backend
capabilities are validated before recording. Commands execute at queue submit,
not while being recorded. A failed submission remains executable and retains
its resources for diagnosis or retry; a successful submission retires its
recorded references after backend completion.

Texture upload/readback layouts use an explicit byte offset, bytes per row, and
rows per image. Zero row fields select a tight layout. Buffer-texture copies add
their buffer offset to the layout offset.

## Current Backend Evidence

| Backend | Implemented resource behavior | Explicit limitations |
|---|---|---|
| Null validation | All declared shapes and format classes; views; budgets; allocator callbacks; mapping/coherence; all transfer families; nearest blit; deferred-retirement validation. | External import/export, linear blit, and compressed clear return `unsupported`. |
| Metal on macOS | Native buffers; native selected single-layer 2D, multisample color, depth/depth-stencil, and swapchain textures and views; shared/private memory; mapping/coherence; buffer copy/fill; texture copy; buffer-texture copy; deterministic readback; render-pass MSAA resolves. | Non-2D/array and unlisted formats, external sharing, and copy-encoder texture clear/resolve/blit remain unsupported. |
| Vulkan on Linux | Native upload/readback/device-local buffers, host-visible linear images, and capability-checked 1D/2D/3D/cube/array/mipmapped/compressed/multisampled images; compatible views; buffer/image and image/image copies; whole-subresource clear; color resolve; nearest/linear color blit. | External sharing, compressed clear, and unsupported runtime format/sample/tiling combinations fail explicitly. |
| D3D12 on Windows | Native upload/readback/default-heap buffers, device-local textures/views, and buffer/texture transfers. | Compressed/external textures and explicit texture clear/resolve/blit remain unsupported. |
| OpenGL/OpenGL ES on Linux | Native upload/readback/device-local buffers, views, copies, and fills. | Textures remain unsupported. |

The macOS test runs with `MTL_DEBUG_LAYER=1` and compares exact bytes after
native buffer, texture, and buffer-texture round trips. This satisfies the
`native_smoke` gate, not full backend conformance.

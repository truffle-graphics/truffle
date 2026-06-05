# ADR 0007 — Render Pass Model: Inline vs Pre-compiled

**Status**: Accepted  
**Date**: 2025-05-22

---

## Context

Phase 3 introduces explicit render-pass control to `ICommandBuffer`. Two design
directions were considered:

### Option A — Pre-compiled pass objects (Vulkan-style)
Create a `IRenderPass` object at device creation time, share it across frames,
and only provide a `begin_render_pass(IRenderPass&, IFramebuffer&)` on
`ICommandBuffer`. Attachments are locked into the pass object.

**Pros**: Matches Vulkan natively; GPU drivers can pre-compile tile-shading
programs on mobile.  
**Cons**: Complex API surface; premature for a codebase with one backend
(null) and imminent Metal work; Metal does not have a VkRenderPass equivalent,
so the abstraction would need to be emulated anyway.

### Option B — Inline descriptor (Metal-style) ← **chosen**
Pass a fully-described `RenderPassDesc` directly to
`begin_render_pass(const RenderPassDesc&)` every frame. The backend consumes
it to configure a `MTLRenderPassDescriptor` (Metal) or synthesise a compatible
Vulkan render pass internally (future).

**Pros**: Maps 1-to-1 to Metal's `MTLRenderCommandEncoder`; simpler client
code; trivially testable on the null backend; compatible with a thin Vulkan
adaptation layer later.  
**Cons**: Pre-compilation opportunities are deferred to the backend; a future
Vulkan backend will need to cache/hash `RenderPassDesc` → `VkRenderPass`
internally.

---

## Decision

Use **Option B** (inline `RenderPassDesc`).

`ICommandBuffer` exposes:

```cpp
Status begin_render_pass(const RenderPassDesc& desc);
Status end_render_pass();
```

`RenderPassDesc` carries:
- `Extent2D extent` — output size for viewport/scissor setup
- `ColorAttachmentDesc` — texture pointer, `LoadOp`, `StoreOp`, and
  `ClearColor`; swapchain rendering passes the acquired drawable texture
  explicitly, while `texture == nullptr` means no concrete color attachment for
  headless/null paths
- `DepthAttachmentDesc` — optional; `texture == nullptr` disables depth

`Renderer::render()` acquires the swapchain drawable once per frame via
`ISwapchain::acquire_next_texture()` and constructs a `RenderPassDesc` before
recording draw calls. When no swapchain is provided (headless / null tests),
it uses a minimal `{1, 1}` extent descriptor that the null backend ignores.

---

## Consequences

- Metal backend (Phase B) maps `RenderPassDesc` directly to
  `MTLRenderPassDescriptor` with zero overhead.
- A future Vulkan backend will hash `RenderPassDesc` fields to look up or
  create a `VkRenderPass` + `VkFramebuffer`, keeping the adapter-level API
  stable.
- Pre-compiled pass objects remain an option for a future Vulkan-specific
  extension without breaking the existing interface.
- Existing tests that call `render(batches)` without a swapchain continue to
  work via the default `nullptr` parameter.

# OpenGL Backend

The desktop GL and GLES backends share one private GL command implementation
(`gl_probe.hpp`, `gl_graphics.hpp`, `gl_program.hpp`). Context creation,
current-context handling and procedure loading live behind a small private
context seam selected by `gl_api.hpp`:

| Context | Header | Platform | Profile |
|---|---|---|---|
| EGL surfaceless pbuffer | `egl_context.hpp` | Linux (GL and GLES) | GL 4.5 core or GLES 3.0 |
| CGL offscreen | `cgl_context.hpp` | macOS | GL 4.1 core |
| NSOpenGLContext on a host `NSView` | `cgl_view.hpp`/`.mm`, `gl_present.hpp` | macOS presentation | GL 4.1 core, shared with the device context |

`gl_api.hpp` also defines feature macros (`TRUFFLE_GL_HAS_COMPUTE`,
`TRUFFLE_GL_HAS_TEXTURE_STORAGE`, `TRUFFLE_GL_HAS_TEXTURE_VIEWS`,
`TRUFFLE_GL_HAS_BASE_INSTANCE`, `TRUFFLE_GL_HAS_DEBUG_OUTPUT`). Code needing a
newer entry point is compiled only where the headers declare it; capabilities
report the rest as unsupported instead of simulating them.

Each context performs a deterministic framebuffer-object clear/readback before
exposing an adapter. Every native operation checks `glGetError`; on EGL,
KHR_debug messages are also forwarded to the instance debug callback.

## Linux (EGL)

`native_smoke`. Owns buffers, samplers, uncompressed textures, core views,
MSAA/resolve and reflected native GLSL programs for indexed/instanced/indirect
depth/stencil/MRT graphics and storage compute. See the support matrix.

## macOS (CGL, deprecated GL 4.1)

`native_smoke`, rendering offscreen and presenting into a host-owned `NSView`.
Built when `OpenGL.framework` is found;
`GL_SILENCE_DEPRECATION` is defined for this target only. Verified on Apple M3
Pro, macOS 27.0.1, adapter `Apple M3 Pro (4.1 Metal - 91.7)`, by
`truffle_opengl_tests` (buffers, textures, direct/expanded graphics, bindings,
compute negatives) with exact readback. Validation on macOS is per-call
`glGetError` checking with zero errors required; no debug-message layer exists
on macOS GL 4.1, so this is not equivalent to KHR_debug or a validation layer.

GL 4.1 lacks immutable texture storage, so textures allocate each level (and
cube face) with `glTexImage*`. Advertised: transfer, descriptor arrays,
dynamic offsets, push constants, presentation; buffer/identity texture views, copies, fills,
clears, resolve and blits; graphics, MRT, depth/stencil, 4x multisample and
indirect draws whose first-instance word is zero. Unsupported and reported as
such: compute and storage blocks, subresource texture views, nonzero base
instance (direct, indexed or indirect; rejected at submit and covered by
`verify_expanded_gl_graphics`), KHR_debug diagnostics, external resources,
`mailbox` presentation.

### Presentation into a host-owned NSView

The host owns the window, its event loop and the `NSView`; Truffle never
creates windows. Pass the view as a `NativeSurfaceKind::cocoa_view` surface
and set `wantsBestResolutionOpenGLSurface = YES` on it if the drawable should
match a Retina framebuffer (Truffle does not change host view settings).

- The swapchain owns an `NSOpenGLContext` (double-buffered GL 4.1 core pixel
  format) that shares objects with the device context, plus one back-buffer
  texture (`color_attachment | copy_source | present`). `imageCount` is
  advisory; there is one image.
- `queue.present` attaches the view if needed, calls `update`, blits the
  image into the default framebuffer with a Y flip (texture row 0 is the top
  of the window), checks `glGetError`, and calls `flushBuffer` and
  `glFinish`, then restores the device context.
- Formats: `rgba8_unorm`, `bgra8_unorm`. Present modes: `fifo` (swap interval
  1), `immediate` (0); `mailbox` returns `unsupported`. Other surface kinds
  return `unsupported`; a handle that is not an `NSView` is
  `invalid_argument`.
- Resize: acquire returns `suboptimal` when the view's drawable no longer
  matches the swapchain; present still scales the image into the drawable and
  returns `suboptimal`; `Swapchain::resize` reallocates the image. A view
  removed from its window has no drawable and returns `out_of_date`.
- Threading: surface and swapchain creation, acquire, resize and present are
  AppKit work and return `invalid_state` off the main thread.

Evidence: `truffle_opengl_presentation_tests` opens a test-owned `NSWindow`
and checks back-buffer pixels (scissored rows land at the top, BGRA matches
RGBA), the off-main-thread, `mailbox`, wrong-kind and non-view negatives,
resize `suboptimal`/recovery and a detached-view `out_of_date`.
`truffle_host_workspace_opengl_window` (`--opengl --frames 120`) presents 120
frames into a GLFW host window across one scripted resize. Both need a
logged-in GUI session.

Apple's GL-on-Metal driver may print `UNSUPPORTED (log once):
gldCopyBufferSubData: NEEDS IMPLEMENTATION`; the buffer test still verifies
the copied bytes exactly.

## Remaining work

- Window presentation through EGL window surfaces on Linux and WGL on Windows
  (#61); macOS host-view presentation is done.
- GL fences, queries and multi-context ownership evidence (#63).
- Windows verification (roadmap Pending Verification). Intel Macs are not a
  target (#171).

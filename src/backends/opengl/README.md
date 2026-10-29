# OpenGL Backend

The desktop GL and GLES backends share one private GL command implementation
(`gl_probe.hpp`, `gl_graphics.hpp`, `gl_program.hpp`). Context creation,
current-context handling and procedure loading live behind a small private
context seam selected by `gl_api.hpp`:

| Context | Header | Platform | Profile |
|---|---|---|---|
| EGL surfaceless pbuffer | `egl_context.hpp` | Linux (GL and GLES) | GL 4.5 core or GLES 3.0 |
| CGL offscreen | `cgl_context.hpp` | macOS | GL 4.1 core |

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

`native_smoke`, offscreen only. Built when `OpenGL.framework` is found;
`GL_SILENCE_DEPRECATION` is defined for this target only. Verified on Apple M3
Pro, macOS 27.0.1, adapter `Apple M3 Pro (4.1 Metal - 91.7)`, by
`truffle_opengl_tests` (buffers, textures, direct/expanded graphics, bindings,
compute negatives) with exact readback and zero GL errors.

GL 4.1 lacks immutable texture storage, so textures allocate each level (and
cube face) with `glTexImage*`. Advertised: transfer, descriptor arrays,
dynamic offsets, push constants; buffer/identity texture views, copies, fills,
clears, resolve and blits; graphics, MRT, depth/stencil, 4x multisample and
indirect draws whose first-instance word is zero. Unsupported and reported as
such: compute and storage blocks, subresource texture views, nonzero base
instance, KHR_debug diagnostics, external resources.

Apple's GL-on-Metal driver may print `UNSUPPORTED (log once):
gldCopyBufferSubData: NEEDS IMPLEMENTATION`; the buffer test still verifies
the copied bytes exactly.

## Remaining work

- Host-window presentation (NSOpenGLContext/CGL attached to a host-owned
  NSView), WGL on Windows (#61).
- GL fences, queries and multi-context ownership evidence (#63).
- Intel Mac and Windows verification (roadmap Pending Verification).

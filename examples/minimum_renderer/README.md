# Minimum Renderer

A small macOS application using Metal, **without ECS or a GUI toolkit**.
The host owns its Cocoa window, `CAMetalLayer`, event loop, backing-pixel extent
and resize/recovery policy. Renderer owns submission, program registration,
mesh bytes and optional view/depth helpers. Shader owns compilation.

The application renders two lit, rotating indexed cubes. Its own vertex,
instance and view/light structures are described explicitly to the pipeline.
Reusable `types.metal`, `transform.metal` and `lighting.metal` units are composed
and compiled through Shader and consumed through Renderer `SourceProgram`.
Those data types and lighting model are not Renderer library contracts.

## Build and run

Requires Apple Silicon macOS with a logged-in GUI session, a Metal device,
the native MSL compiler/reflection runtime (macOS 26+), an installed Apple SDK,
and the repository's initialized pinned dependencies. No offline Metal Toolchain
or portable shader compiler is needed for this example.

From the repository root, using an existing suitable build directory or a new one:

```sh
cmake -S . -B build/minimum-renderer \
  -DTRUFFLE_BUILD_BACKEND_METAL=ON \
  -DTRUFFLE_SHADER_ENABLE_METAL_COMPILER=ON \
  -DTRUFFLE_BUILD_EXAMPLES=ON
cmake --build build/minimum-renderer --target truffle_minimum_renderer
./build/minimum-renderer/examples/minimum_renderer/truffle_minimum_renderer
```

Resize the window normally; Escape or the close button exits. The host updates
the swapchain, depth allocation and camera aspect using backing pixels. Zero
size/minimization suspends drawing. Out-of-date swapchains are reported and
recreated; other failures exit with a diagnostic.

`--smoke` presents exactly 12 frames, changing the window size twice, and fails
unless both real drawable/depth changes occurred. It requires a real display:

```sh
ctest --test-dir build/minimum-renderer --output-on-failure \
  -R '^truffle_minimum_renderer_window$'
```

The separate `truffle_renderer_scene_metal_tests` executable shares `scene.cpp`
and the shader units with this app. It checks every output pixel for instance
properties, quaternion interpretation, lighting, camera movement, perspective,
depth occlusion, target recreation and a spatial cube reference.

## Deliberate limits

The example is serialized with a graphics completion fence before updating CPU
data. It uses upload-domain mesh storage, one directional-light model and
per-update object-buffer allocation for clarity, not an optimized frame
scheduler. There is no material system, asset importer, textures, scene engine,
GUI or ECS requirement. The shader file paths are rooted in this source checkout;
this is not an installed application bundle. Other-platform consumption and
performance/stress evidence remain separate work.

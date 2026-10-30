# Prepared TSP Renderer consumer

This small headless consumer loads `.tsp` files, selects prepared vertex and
fragment configurations, registers an owned program, destroys the caller
packages and executes one `RenderBatch` through a `FrameGraph`. It uses
`truffle_renderer` and an explicitly enabled native backend, with no Shader
compiler library, EnCom, windowing or Assets dependency.

The shader must generate triangle vertices from vertex IDs and need no descriptor
resources; this intentionally keeps the example focused on package consumption.
`docs/renderer.md` describes descriptor-group batches and safe replacement.

Configure with `TRUFFLE_BUILD_EXAMPLES=ON` and Metal and/or Vulkan enabled, then:

```bash
cmake --build build/renderer --parallel 4 --target truffle_prepared_renderer
build/renderer/examples/prepared_renderer/truffle_prepared_renderer \
  msl vertex.tsp vertex_main fragment.tsp green_fragment 0 configured
```

The arguments select target (`msl`, `metallib` or `spirv`), vertex package/entry,
fragment package/entry, a configuration slot and an optional TSP permutation.
Both packages can be the same file. Target/backend unavailability and package or
native failures return a nonzero exit code.

The native Renderer test generates a real `prepared.tsp` fixture in
`build/renderer/tests/renderer-native-fixtures/`. To run this example against that
fixture, replace both package paths above with its path. No offline/source
compiler is needed to generate that hand-authored native-source fixture.

The example reports submitted batch count, not a pixel assertion.
`truffle_renderer_metal_tests` verifies the actual native output and failure/
replacement behavior with exact full-target RGBA8 readback. The Vulkan example
selection is available when built, but this track's native evidence is Metal.

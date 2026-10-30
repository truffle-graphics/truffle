# Standalone Shader workflow

This CPU-only consumer links only `Truffle::Shader`. It needs no Renderer,
backend, device, EnCom, window, shaderc executable, or background service.
Enable the optional glslang adapter to run its GLSL-to-SPIR-V requests.

The fragment source includes a reusable color module, which includes a tint
module. These are native GLSL includes and guarded functions, not a Truffle
language. The default filesystem resolver tracks all three canonical source
identities and their content. `USE_TINT` selects the requested configuration.
Only the two configurations explicitly requested by this example are compiled.

From a repository build configured with `TRUFFLE_BUILD_EXAMPLES=ON` and
`TRUFFLE_SHADER_ENABLE_GLSLANG=ON`:

```sh
cmake --build build --target truffle_shader_workflow --parallel 2
build/examples/shader_workflow/truffle_shader_workflow \
  examples/shader_workflow/shader.frag
```

Or build against an existing installed Truffle package, without configuring
the rest of the repository:

```sh
cmake -S examples/shader_workflow -B build/shader-workflow \
  -DCMAKE_PREFIX_PATH=/path/to/truffle/install
cmake --build build/shader-workflow --parallel 2
build/shader-workflow/truffle_shader_workflow \
  examples/shader_workflow/shader.frag
```

The program keeps one `Compiler` for its instance-owned in-memory cache and
one `RebuildState` for its logical shader. An unchanged request hits the cache;
an explicitly changed define produces a distinct package. It then deliberately
rejects an invalid edit and an unsupported reflected combined sampler, verifying
that neither replaces the last-good result. Restoring the valid source recovers
the cached package. Expected rejection diagnostics are printed explicitly;
unexpected failures or an unavailable GLSL route return nonzero.

No example files are changed, no watcher is started, and no package is written
to disk. Results own their TSP bytes and canonical reflection. A real host
re-reads the root and explicitly calls `rebuild()` when polling; dependencies
are re-resolved before cache lookup. Renderer/RHI consumption and GPU-safe
replacement remain separate responsibilities.

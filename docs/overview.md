# Overview

Truffle is a CMake-first layered graphics library workspace. It starts from
embeddable contracts, independently linkable modules, and validation paths so
consumers can choose an application host, windowing policy, simulation model,
and asset workflow independently.

Current baseline:

- General ECS entity, component, query, and system execution primitives.
- Modular RHI 1 move-only objects backed by opaque generation-checked handles
  and private dispatch tables, with explicit command, synchronization, surface,
  and presentation foundations.
- Strict Null validation of object lifetime, command state, resource retention,
  thread ownership, acquisition, submission, and presentation flow.
- Native Metal buffers and selected 2D textures with validation-enabled
  deterministic upload/copy/readback proofs at `native_smoke` on macOS.
- Explicitly unavailable Vulkan, OpenGL, and Direct3D targets that report no
  simulated adapters until their native implementations land.
- Deterministic ShaderPackage 1.0 creation/loading with normalized cross-target
  reflection, capability gates, native-variant precedence, and an optional
  dependency-free package assembly and inspection tool.
- Renderer flow that currently proves ECS extraction while reserving non-ECS and
  high-throughput render-data lanes for later rendering work.
- Declarative asset, material-operation, geometry-stream, and group/tag
  descriptors for higher-level consumers that need metadata before GPU upload or
  backend-specific loading exists.
- Metadata-only asset-to-render planning that maps declared streams into
  `RenderBatch` layouts without allocating buffers, compiling shaders, or
  depending on a backend.
- Opt-in diagnostics helpers for asset catalogs, render batches, asset-render
  plans, frame graphs, renderer stats, and debug overlay declarations, including
  bundle reports and external labels, without making debug/profiling code a
  runtime dependency.
- Host-owned workspace example that uses a visible GLFW window for editor, CAD,
  and simulation-facing app roles while the Truffle path stays null-backed.
- Dense-data diagnostics example that declares lidar/radar-style grouped streams
  and tool overlays without per-detection CPU scans or backend work.
- Installable CMake targets plus presets and CI paths for the current foundation.

See `docs/rhi1/` for the replacement contract, dependency policy, shader
package contract, target matrix, and evidence-based maturity gates.

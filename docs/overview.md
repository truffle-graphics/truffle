# Overview

Truffle is a CMake-first layered graphics library workspace. It starts from
embeddable contracts, independently linkable modules, and validation paths so
consumers can choose an application host, windowing policy, simulation model,
and asset workflow independently.

Current preliminary baseline:

- General ECS entity, component, query, and system execution primitives.
- Backend-neutral preliminary RHI contracts for device, resource, surface,
  swapchain, command, queue, fence, and capability flow. These are being
  replaced by the breaking RHI 1 contract.
- Null backend used to validate contracts without a production GPU backend.
- Native Metal implementation at `cross_compiles` on macOS, pending accepted
  validation-enabled native smoke evidence.
- Headless Vulkan, OpenGL, and Direct3D contract simulators that do not yet call
  their named graphics APIs.
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
package direction, target matrix, and evidence-based maturity gates.

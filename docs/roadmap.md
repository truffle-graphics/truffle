# Roadmap

## Phase 1: Foundation And Contracts — Complete

- Establish modular CMake targets and public include layout.
- Make current library layers installable and consumable through CMake package exports.
- Add developer presets, quality checks, and cross-platform CI workflow files.
- Validate ECS entity, component, query, and system boundaries.
- Validate RHI resource, command, surface, swapchain, and submission contracts through the null backend.
- Prove host-owned surface embedding with a structured workspace example.

## Phase 2: Render Data Foundation — Complete

- Define `RenderBatch` and `InstanceLayout` as the universal renderer input contract, decoupling `truffle_render` from `truffle_ecs`.
- Establish `IFrameUploadRing` as the N-buffered CPU-to-GPU upload primitive at the RHI layer, supporting both ECS extraction and direct bulk upload.
- Introduce `truffle_scene` as the optional ECS-to-render bridge module.
- Define `IPipelineCache` interface for shader variant management.
- Expand ECS component definitions for cameras, lighting, meshes, and materials.

## Phase 3: Core Rendering Mechanics — Complete

- Material system support mapping shader parameters without hardcoding colors.
- Asynchronous fence dispatch routines across CPU and backends.
- Index draw primitives supporting `IndexFormat` and instanced indexed geometries.
- Embedded platform abstraction hooking into Metal view layers explicitly mapping native application shells to graphics.

## Phase 4: Production Backends — Complete

- Add memory pools reducing strict resource allocations at runtime.
- Add Indirect Draw buffers optimizing repeated dispatches avoiding CPU roundtrips.
- Support Shader Pipeline Variants executing hot-reload layouts using semantic layout hashes.
- Implement Metal for macOS.
- Implement Vulkan stubs bootstrapping initialization tests.
*(DirectX remains deferred to an extended target build as needed)*

## Phase 5: Compute & Frame Graph Orchestration — Complete

### 5A: GPU Transform Hierarchy — Complete

- Implement GPU transform hierarchy compute pass for large node graphs processing local transform coordinate space arrays cleanly.

### 5B: Frame Graph Orchestration — Complete

- Grow frame graph and render pass orchestration from the current submission baseline.

### 5C: Shader Reflection & Binding Layout Direction — Complete

- Add shader reflection and binding layout direction without binding the public API to one GPU backend.

### 5D: Vulkan Parity — Complete

- Advance Vulkan capabilities to parity with the established Metal backend output.

## Phase 6: API Stabilization & Validation Completeness — Complete

- Finalize public API stability boundaries and versioning policy.
- Enforce binding validation across render and compute flows with deterministic failure paths.
- Add compatibility and contract tests for stable public APIs.
- Added initial null/Vulkan shared RHI contract tests for deterministic error semantics.
- Expanded null/Vulkan shared RHI contract tests for swapchain lifecycle and queue/fence submission parity.
- Added `truffle/core/version.hpp` with explicit API-version compatibility rules and CI coverage.
- Added explicit deprecation-window policy helpers and CI tests for symbol availability/deprecation states.
- Added optional-backend compile gating in shared RHI contract tests to keep status semantics portable across null/Vulkan/Metal/OpenGL participation.

## Phase 7: Vulkan Production Confidence — Complete

- Complete Vulkan backend production hardening and backend parity verification against Metal.
- Gate Vulkan behavior via backend parity tests and CI coverage.
- Added Vulkan reflection and lifecycle contract checks to CI validation paths.
- Added reflection invariant checks in shared backend contract tests to tighten parity gates.
- Added generated backend parity matrix artifacts in CI for contract/reflection test visibility.
- Parity report tracking now includes OpenGL parity test status alongside Vulkan/Metal coverage.

## Phase 8: Secondary Backend Expansion — Complete

- Deliver additional production backend coverage beyond Metal and Vulkan.
- Maintain backend-neutral public API boundaries during expansion.
- Expanded shared backend contract coverage to include Metal when available.
- Implemented `truffle_backend_opengl` with queue/command/resource/surface/swapchain/upload-ring/pipeline reflection contract coverage.
- Added `truffle_opengl_tests` and shared contract integration for OpenGL.

## Phase 9: Performance & Dense Workload Readiness — Complete

- Introduce memory and submission optimizations for dense workloads.
- Establish measurable performance gates and regression checks.
- Optimized render/compute reflection binding validation paths to fixed-size arrays.
- Added `truffle_performance_sanity_tests` to enforce a measurable CPU-side upload/submit budget gate in CI.

## Phase 10: Advanced Rendering Feature Set — Complete

- Add dependency-aware multi-pass workflows (for example shadows and post-processing).
- Extend frame orchestration for multi-camera and resource hazard-safe scheduling.
- Added dependency-aware frame-graph scheduling with cycle detection and tests.
- Added resource usage tracking with implicit read/write hazard ordering in frame-graph scheduling.
- Explicit caller dependencies now take precedence over implicit hazard edges to avoid false scheduling cycles.
- Added `truffle_advanced_render_flow_tests` validating multi-pass compute-to-render hazard-safe scheduling and execution stats.

## Phase 11: Tooling, Debugging & Profiling — Complete

- Add runtime diagnostics, shader iteration tooling, and profiling support.
- Improve production triage capabilities across supported backends.
- Added per-frame renderer diagnostics (`RendererFrameStats`) and validation tests.

## Phase 12: Packaging, Distribution & Ecosystem — Complete

- Deliver package manager integration and release automation.
- Publish versioned distribution artifacts and end-to-end consumer onboarding guides.
- Added CPack baseline configuration for distributable package generation.
- Added CI package workflow job that builds/installs and publishes generated CPack archives.
- Added tag-driven release workflow to generate package archives and publish GitHub Releases.
- Added distribution/onboarding guidance for package consumption and release paths.

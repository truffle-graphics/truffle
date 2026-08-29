# ADR 0011: RHI 1 Replacement Contract

## Status

Accepted

## Context

The preliminary RHI proved backend-neutral state machines and a native Metal
path, but it accumulated renderer policy inside one large public header while
several nominal backends remained CPU-side contract simulators. Shared contract
coverage is useful evidence, but it is not evidence of native GPU execution,
presentation, or platform support.

Truffle needs one canonical low-level graphics API that can support explicit
native backends without preserving the preliminary API's shape or policy
engine.

## Decision

The preliminary API is replaced by **Truffle RHI 1** through an immediate
breaking cutover. There is no compatibility facade, parallel public API, or
backend-facing Simple RHI. Higher Truffle modules migrate in the same issue
slice that changes the contract they consume.

RHI 1 uses:

- focused public headers with an umbrella include;
- move-only RAII objects backed by opaque, generation-checked handles and a
  backend dispatch table;
- `Instance`, `Adapter`, `Device`, `Queue`, `CommandPool`, `CommandList`, scoped
  encoders, resources and views, pipelines, synchronization objects, `Surface`,
  and `Swapchain` as the primary object families;
- portable typed outcomes at the public boundary, with optional backend
  diagnostics kept as a second layer;
- explicit feature negotiation and `unsupported` results instead of simulated
  success;
- immutable ordinary bind groups and caller-controlled descriptor arenas;
- explicit pipeline stages, access masks, texture layouts, subresource
  barriers, queue ownership transfers, and semaphore values;
- presentation through `Queue::present()`, separate from command recording;
- a format-neutral `ShaderPackage` runtime contract, with offline compilation
  provided by the optional `truffle-shaderc` host tool.

Resource creation and queue submission are thread-safe. Command pools, command
lists while recording, and active encoders are single-thread-owned. Resource
retirement is deferred until every queue timeline value that references the
resource has completed.

Backend and platform maturity is evidence-based and independent for every
backend-platform pair. Shared interface tests alone cannot promote a native
backend. The states are `source_only`, `cross_compiles`, `native_smoke`,
`conformant`, and `supported`; their objective gates live in
`docs/rhi1/support-matrix.md`.

Runtime and tool dependencies are backend-optional, pinned, and sourced from
checked-out bundled source by default. Configure never downloads source.
Unavoidable platform SDKs are discovered explicitly. An expert system-package
mode may be enabled deliberately, but it is not the reproducible default.

The host retains window, event-loop, input, and application-lifecycle
ownership. Native surfaces are borrowed host boundaries with explicit lifetime
rules.

## Out Of Scope

- A Simple RHI or convenience API above the canonical RHI.
- Compatibility shims for the preliminary public RHI.
- Renderer caching, descriptor admission, eviction, or recovery policy inside
  core RHI.
- D3D11, WebGL1, older GL/GLES profiles, CUDA/OpenCL, and public proprietary
  console backends.
- Promotion based only on compilation or contract-simulator behavior.

## Consequences

- Existing RHI consumers must migrate during the cutover.
- Backends can advance independently without overstating other platforms.
- Native correctness, output validation, and presentation proof happen before
  optimization or support claims.
- The dispatch seam can support a stable C ABI later without publishing one as
  part of RHI 1.
- The preliminary descriptor policy engine is deleted rather than carried into
  the replacement.

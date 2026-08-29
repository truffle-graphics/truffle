# Truffle RHI 1

RHI 1 is the breaking replacement for Truffle's preliminary graphics API. It
is the only backend-facing graphics contract. The public object and dispatch,
resource, shader-package, binding, pipeline, synchronization, and presentation
slices tracked by issues #28-#32 are implemented. Native backend breadth and
advanced capability work remain in progress.

`AdapterInfo` includes the backend/platform pair and its evidence-based
maturity. `backend_platform_support()` exposes the full repository matrix even
when a backend target is not linked. That table is repository evidence, not a
runtime availability promise; factories expose adapters only after native
initialization succeeds. `truffle-rhi-doctor` emits both views as JSON.

## Ownership And Object Model

- `Instance` owns backend discovery, validation configuration, debug callbacks,
  and surface creation.
- `Adapter` is an immutable snapshot of hardware identity, limits, memory
  topology, and supported features.
- `Device` owns negotiated features, resource creation, retirement, and
  device-loss state.
- `Queue` owns ordered submission, semaphore signaling, and presentation for a
  graphics, compute, or transfer queue.
- `CommandPool` owns command allocation and reset. `CommandList` owns recorded
  commands. `RenderEncoder`, `ComputeEncoder`, and `CopyEncoder` scope active
  recording.
- Buffers, textures, views, samplers, binding objects, pipelines, fences,
  semaphores, query pools, surfaces, and swapchains are move-only RAII values.

Public objects contain no backend-native types. They refer to opaque,
generation-checked handles and a backend dispatch table. A stale generation is
an invalid object, never a route to reused native storage.

## Threading Contract

The following operations are safe to call concurrently unless a narrower
object rule is documented:

- immutable `Instance` and `Adapter` queries;
- device feature, limit, budget, and loss-state queries;
- resource, view, sampler, binding-layout, pipeline-layout, pipeline, fence,
  semaphore, and query-pool creation;
- queue submission and presentation. Calls targeting the same queue are
  internally serialized into submission order.

The following are single-thread-owned:

- a `CommandPool`, including allocation and reset;
- a `CommandList` from begin through end or reset;
- every active render, compute, or copy encoder;
- caller-controlled descriptor-arena mutation and reset.

Ownership can move between threads only while the object is idle. Ordinary
bind groups are immutable after creation. Mutable or update-after-bind tables
belong to an optional feature with its own synchronization requirements.

## Lifetime Contract

- Parents outlive child creation calls, but destroying a public wrapper does
  not immediately destroy native storage still referenced by submitted work.
- Each submission records the last queue timeline values that use an object.
  `Device` retires native storage only after all recorded values complete.
- Destroying or moving a wrapper invalidates that wrapper immediately.
- A `CommandList` retains references needed by its recorded commands through
  submission. Reset releases unsubmitted references.
- A descriptor-arena reset advances its epoch and immediately invalidates bind
  groups allocated from earlier epochs. Recorded commands retain resources but
  do not make an invalidated group bindable again.
- Swapchain images are borrowed for one acquisition epoch. Resize,
  `out_of_date`, or swapchain destruction invalidates outstanding image views.
- A native surface handle is borrowed from the host. The host keeps the native
  window/layer alive until the RHI `Surface` and dependent swapchains are gone.
- Device loss stops new work, preserves diagnostic queries, and makes remaining
  resource retirement deterministic.

## Outcomes And Diagnostics

RHI 1 does not use exceptions across its public boundary.

- Object creation and value-returning queries return `Result<T>`.
- Operations without a value return `Status`.
- Swapchain acquisition returns `AcquireResult`, containing image, index,
  synchronization value, and a presentation status.
- `Queue::present()` returns `Status`.

Portable statuses include `success`, `unsupported`, `invalid_argument`,
`invalid_state`, `suboptimal`, `out_of_date`, `surface_lost`, `device_lost`,
`out_of_memory`, `timeout`, and `backend_validation_failed`. A second optional
diagnostic layer carries backend name, native code, object label, and a bounded
message. Portable control flow never depends on parsing that diagnostic text.

## Resources, Binding, And Synchronization

RHI 1 owns mechanism rather than renderer policy:

- buffers and textures expose explicit usage and memory domains;
- views own format reinterpretation and subresource ranges;
- mapping, persistent mapping, flush, invalidate, upload, readback, copies,
  clears, resolves, blits, and fills are explicit;
- logical bindings use `(group, binding, arrayElement)` and never imply native
  slot identity;
- ordinary bind groups are immutable, while descriptor arrays, bindless tables,
  push constants, dynamic offsets, specialization constants, and immutable
  samplers are capability-gated;
- callers own resource allocators and descriptor arenas;
- synchronization uses pipeline-stage flags, access flags, texture layouts,
  subresource and aliasing barriers, queue ownership transfers, and explicit
  wait/signal semaphore values.

Descriptor caching, admission, quota, eviction, recovery, and materialization
policy belongs above RHI. The preliminary policy engine was deleted during the
public cutover.

## Presentation Boundary

The host owns native windows, layers, views, event loops, resize policy, and
application lifecycle. RHI owns only the graphics-side `Surface`, `Swapchain`,
acquisition, and presentation objects. Presentation is a queue operation and is
never encoded into a command list.

## Related Specifications

- [Resources, memory, views, and transfers](resources.md)
- [Bindings and graphics/compute pipelines](bindings-and-pipelines.md)
- [Synchronization and presentation](synchronization-and-presentation.md)
- [Backend and platform maturity](support-matrix.md)
- [Dependency policy](dependencies.md)
- [Shader package contract](shader-package.md)
- [ShaderPackage 1.0 binary format](shader-package-format.md)
- [ADR 0011](../adr/0011-rhi-1-replacement-contract.md)

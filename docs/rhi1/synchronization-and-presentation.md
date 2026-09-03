# Synchronization And Presentation

RHI 1 records transfers, barriers, render commands, and compute commands in one
ordered command-list stream. Submission cannot reorder a transfer across an
encoder boundary. Barriers are recorded only while no encoder is active.

## Portable synchronization

`PipelineStage`, `Access`, and `TextureLayout` describe dependencies without
publishing backend-native values. `BarrierBatch` groups buffer ranges, texture
subresource ranges, aliasing boundaries, and optional queue-ownership
transfers. Null tracks texture layout per mip and array layer and rejects stale
old-layout declarations or use from the wrong queue owner.

`QueueSubmitDesc` contains the command-list array, timeline-semaphore wait and
signal arrays, an optional timeline fence signal, and a bounded CPU wait time.
Signal values must increase. A timed-out wait leaves every command list
executable so the caller can retry or diagnose it. The older compact submit
overload remains only as a convenience for submissions with no semaphore
dependencies.

The current foundation completes submission before returning. Fences therefore
represent completed GPU work, not merely accepted work. This is correct but
intentionally conservative; asynchronous queue workers and deferred object
retirement remain hardening work.

`QueryPool` results are explicit command-stream data. Timestamp writes occur
between encoders, occlusion queries begin and end inside one render encoder,
and `CommandList::resolve_queries()` writes packed 64-bit results into a buffer
with `copy_destination` usage. Query type/index, nesting, resolution range,
buffer usage, alignment, and availability are validated. Pipeline-statistics
queries remain optional and fail when a backend does not implement them.

## Direct3D 12 mapping

- Timeline semaphores own `ID3D12Fence` values. Submissions encode queue waits
  and signals; the portable bounded wait preserves timeout-without-consumption
  so an executable command list can be retried.
- Buffer and texture dependencies use legacy `ResourceBarrier` transitions,
  with UAV barriers for write-after-write ordering and native aliasing barriers.
  Queue ownership transfer remains unsupported while the backend exposes only
  one direct queue.
- Legacy barriers are the baseline fallback for the declared Windows SDK 19041
  floor. Enhanced barriers are not selected opportunistically; they require a
  separately tested capability path before they can replace this deterministic
  fallback.
- Timestamp and occlusion pools map to D3D12 query heaps and resolve through
  `ResolveQueryData`. Timestamp stage hints identify command-stream placement;
  D3D12 supplies no finer portable per-stage timestamp point.
- Validation-enabled instances turn on both the D3D12 debug layer and
  GPU-based validation when the installed debug interface exposes it.

## Presentation

Acquisition returns a borrowed image, image index, status, and an available
timeline semaphore/value. `suboptimal` acquisitions remain usable. Rendering
waits on the acquisition point, transitions the image from `present` to
`color_attachment`, transitions it back, signals a rendered point, and passes
that point to `Queue::present()`. Presentation is never a command-list opcode.

Resize is rejected while an image is acquired. `out_of_date` is recoverable by
resizing or recreating the swapchain. `surface_lost` requires recreating the
host surface and swapchain. `device_lost` marks the RHI device lost; callers
must create a new device and dependent objects.

## Metal mapping

- Timeline semaphores use `MTLSharedEvent` wait/signal values.
- Encoder boundaries preserve explicit barrier order. Metal resource hazard
  tracking supplies native memory ordering; RHI layout and ownership state is
  still validated independently.
- A `cocoa_layer` surface retains the host-provided `CAMetalLayer`. Swapchain
  acquisition uses `nextDrawable`, and queue presentation uses
  `presentDrawable` on a Metal command buffer.
- `fifo`, `mailbox`, and `immediate` configure the layer's display-sync policy
  to the degree exposed by `CAMetalLayer`; unsupported swapchain formats fail
  explicitly.
- Drawable extent drift returns `suboptimal`, a zero drawable extent returns
  `out_of_date`, acquisition exhaustion returns `timeout`, a detached layer
  returns `surface_lost`, and Metal device-removal errors return `device_lost`.

The validation-enabled macOS suites prove mixed render-to-copy ordering,
shared-event semaphore/fence completion, native layer acquire/render/present,
suboptimal acquisition, resize recovery, `out_of_date`, and surface-loss
recreation. The
surface test uses a real `CAMetalLayer` without claiming ownership of a native
application window or event loop. A private, non-installed fault hook validates
the nondeterministic out-of-date acquisition and device-loss propagation paths,
then exercises real resize and fresh-device recovery without pretending that CI
physically resized the layer to zero or removed the GPU.

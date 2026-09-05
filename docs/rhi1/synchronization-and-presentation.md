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
`AdapterInfo::timestampPeriodNanoseconds` converts one raw timestamp tick to
nanoseconds and is positive whenever `timestamp_queries` is advertised.

## Vulkan mapping

- Queue discovery prefers dedicated compute and transfer families and falls
  back to a compatible family only when the adapter exposes no dedicated one.
  Only discovered queue kinds are advertised. Buffers and images use concurrent
  sharing when more than one family is selected, while RHI queue ownership
  remains explicitly validated at the portable boundary.
- Portable buffer ranges, texture subresources, layouts, stages, access masks,
  and aliasing boundaries map to `vkCmdPipelineBarrier`. Automatic attachment,
  descriptor, transfer, and host transitions use the same tracked native image
  layouts.
- Timeline semaphores use Vulkan 1.2 or
  `VK_KHR_timeline_semaphore`. Submission carries the selected queue kind to the
  backend, submits native wait/signal values and stage masks, and waits for that
  queue to complete before publishing the foundation fence value.
- Timestamp support is advertised only when the graphics family reports valid
  timestamp bits. Timestamp and occlusion pools are reset before use and
  resolve packed 64-bit values with `VK_QUERY_RESULT_WAIT_BIT`, so unavailable
  results are never exposed as completed data. Pipeline statistics remain
  unsupported. The adapter publishes Vulkan's native `timestampPeriod` for
  calibration.
- Linux `xcb` surfaces borrow the host's `xcb_connection_t` and window ID; the
  host continues to own window creation, events, resizing, and destruction.
  The backend enables surface and XCB instance extensions plus the swapchain
  device extension only when available, verifies graphics-family presentation,
  and requires renderable, transfer-source swapchain images for the native
  readback contract.
- Swapchain acquisition waits on a native fence before returning the completed
  RHI acquire timeline point. Presentation submits RHI timeline waits and
  signals a binary semaphore required by `vkQueuePresentKHR`. FIFO is the
  guaranteed tested mode; unavailable requested modes and non-XCB surface kinds
  return `unsupported`. Resize waits for idle work and replaces the native
  swapchain using the previous swapchain handle.

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
  D3D12 supplies no finer portable per-stage timestamp point. The adapter
  publishes the inverse queue timestamp frequency as nanoseconds per tick.
- Validation-enabled instances turn on both the D3D12 debug layer and
  GPU-based validation when the installed debug interface exposes it.
- A `win32` surface borrows a live host-provided `HWND`; Truffle does not create
  the window, pump its messages, or control application lifetime. Swapchains
  use `CreateSwapChainForHwnd` with flip-discard buffers, and acquired images
  are borrowed D3D12 back buffers released after presentation so
  `ResizeBuffers` can proceed safely.
- `fifo` uses a synchronized present interval. `immediate` is available only
  when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` succeeds and sets both swapchain
  and present tearing flags. DXGI exposes no portable mailbox mode, so
  `mailbox` returns `unsupported`.
- Supported swapchain formats are RGBA8/BGRA8 UNORM or sRGB views and
  RGBA16-float. Flip-model image counts must be between 2 and 16. Client-area
  drift is `suboptimal`; zero extent is `out_of_date`; destroyed windows,
  allocation failure, busy presentation, and device removal map to
  `surface_lost`, `out_of_memory`, `timeout`, and `device_lost` respectively.

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

The validation-enabled Windows suite owns only its test window and message
pump. It proves Win32 surface creation, a WARP-backed flip-model swapchain,
exact BGRA8 clear/readback before native presentation, semaphore-connected
acquire/render/present, client-size drift, `ResizeBuffers` recovery, destroyed
window detection, and typed status/device recovery. An occluded CI desktop is
reported as usable `suboptimal` presentation rather than false success.

The validation-enabled Linux suite owns only its tiny XCB test window and runs
under Xvfb. It proves native acquire, a BGRA8 clear, exact padded-row readback,
timeline-to-binary present synchronization, FIFO presentation, host-window
resize, and native swapchain recreation. It then repeats the same exact output
and presentation proof at the resized extent. Wayland and non-Linux Vulkan WSI
remain separate platform work. Private, non-installed fault hooks cover
`timeout`, `suboptimal`, `out_of_date`, `surface_lost`, `out_of_memory`, and
`device_lost` propagation at acquisition/presentation boundaries without
standing in for the native success, readback, resize, or presentation proof.

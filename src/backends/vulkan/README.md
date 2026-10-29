# Vulkan Backend

This target privately compiles pinned Vulkan-Headers and volk. The factory
loads a native runtime, enables `VK_LAYER_KHRONOS_validation` when requested and
available, handles portability enumeration/subset extensions, discovers a real
graphics adapter, creates a device/queue, and submits a native command buffer
before exposing the RHI adapter.

The current Linux lane is `native_smoke`. It owns upload/readback and
device-local buffers, buffer views, host-visible linear images, and
capability-checked 1D, 2D, 3D, cube, array, mipmapped, compressed, and
multisampled images. Compatible
linear/sRGB image views, buffer/image and image/image subresource copies,
uncompressed whole-subresource clear, color resolve, and nearest/linear color
blit execute natively. Tests compare exact buffer output, padded-row and
mip/layer/volume/compressed round trips, clear/blit output, and multisample
resolve when the software adapter exposes the requested format/sample count.
Depth and stencil aspects have explicit clear/readback coverage, and the Linux
lane requires BC1 plus four-sample resolve support rather than silently skipping
those paths.

Host texture reads and writes use queried linear-image subresource layouts,
preserve caller row/image padding, and explicitly synchronize host access with
the graphics queue. The backend owns its Vulkan allocation boundary directly:
it queries memory requirements and memory types, allocates and binds each
resource, and releases the allocation with the resource. Logical budget
reservation occurs before native allocation and rejects exhaustion
deterministically. The backend owns samplers, descriptor-set and pipeline
layouts, shader modules, compute pipelines, and one-color single-sample
graphics pipelines. Generated SPIR-V fixtures prove sampled-texture/sampler and
storage-buffer bindings, push constants, direct/indirect compute, and
direct/indexed/instanced/indirect triangle output with exact readback. Native
depth/stencil state and attachments, multiple render targets, and multisample
resolve are mapped through compatible transient render passes; Linux validation
proves exact depth pass/fail, red/green MRT, and four-sample resolved output.
Compute-to-render ordering inserts explicit shader visibility and produces an
exact storage-driven fragment result. Arena-reset lifetime behavior and
reflection/layout plus optional-feature failures are covered without native
simulation. Non-identity binding remaps, bindless/update-after-bind,
indirect-count and pipeline caches remain unsupported. The backend discovers
graphics, compute, and transfer queue families, uses concurrent resource
sharing across distinct families, maps explicit buffer/texture/aliasing
barriers, owns timeline semaphores, and resolves native timestamp and occlusion
queries. Validation proves cross-queue timeline ordering, multi-list execution,
timeout retry, exact copied output, explicit transitions, timestamp ordering,
timestamp-period calibration, bounded nonzero occlusion, and deterministic
device-loss propagation plus fresh-device recovery. WSI and presentation
use host-owned XCB connection/window handles on Linux. The backend selects
surface capabilities, exact BGRA format, FIFO mode, image count, extent, and
usage; acquires with a native fence; bridges RHI timeline waits to a WSI binary
semaphore; and recreates the native swapchain after host resize. Validation
under Xvfb proves exact padded BGRA8 readback before present and repeats the
proof after resize. Wayland and other platform WSI paths remain unsupported.
RHI 1 exposes no buffer-device-address contract. External sharing remains
unadvertised until platform handle types and ownership are defined.

## macOS Through MoltenVK

On macOS the backend links no Apple framework; it loads the host Vulkan loader,
which selects the MoltenVK ICD. Portability enumeration and
`VK_KHR_portability_subset` are enabled when exposed. If volk's default search
fails, the backend tries `/opt/homebrew/lib/libvulkan.1.dylib` and then
`/usr/local/lib/libvulkan.1.dylib`. Presentation uses `VK_EXT_metal_surface`
with a host-owned `NativeSurfaceKind::cocoa_layer` (`CAMetalLayer*`); other
surface kinds return `unsupported`.

Local evidence (2026-10-29, Apple M3 Pro, macOS 27.0.1, Homebrew MoltenVK
1.4.2, loader 1.4.363.0):

```sh
cmake -S . -B build/vulkan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DTRUFFLE_WARNINGS_AS_ERRORS=ON -DTRUFFLE_BUILD_TESTS=ON \
  -DTRUFFLE_BUILD_BACKEND_VULKAN=ON -DTRUFFLE_BUILD_SHADERC=ON \
  -DTRUFFLE_SHADERC_ENABLE_GLSLANG=ON
cmake --build build/vulkan
ctest --test-dir build/vulkan --output-on-failure
```

`truffle_vulkan_tests` runs the same shared suite as Linux plus a
`CAMetalLayer` presentation proof (exact readback before present and after a
64x48 to 80x60 resize, then status/fault paths). Apple GPUs do not support
`D24_UNORM_S8_UINT`; texture creation returns `unsupported` and the test uses
`D32_SFLOAT_S8_UINT`. Timestamp queries are not advertised when the driver
reports `VK_DRIVER_ID_MOLTENVK` (read through `VK_KHR_driver_properties`,
because the instance targets Vulkan 1.1): MoltenVK 1.4.2 intermittently
resolves the second of two timestamps to 0 when `vkCmdCopyQueryPoolResults`
runs in the same submission, about 3-10% of runs, with no validation message.
The feature request and timestamp pool creation return `unsupported`, and the
test skips timestamp checks from the advertised features. The suite is clean under Homebrew
`VK_LAYER_KHRONOS_validation` 1.4.363.0 (2026-10-30), also with
`VK_LAYER_VALIDATE_SYNC=1`, so macOS is `native_smoke`. The ctest fails on any
`Validation Error`/`Validation Warning` output, and it sets
`DYLD_FALLBACK_LIBRARY_PATH` to the host loader directory because Homebrew's
layer manifest names its dylib without a path. If an enumerated layer cannot
load, the backend creates the instance without it, because validation is
optional and a broken layer must not make Vulkan unavailable. The macOS test
fails in that case, so validation evidence is never dropped silently. iOS-family targets remain
`source_only`; MoltenVK packaging and iOS-family lanes are tracked in #172.
Intel Macs are not a target (#171).

Injected `suboptimal`/`out_of_date` present results still present the image
natively before reporting the status, so fault tests never leave a swapchain
image acquired.

Other platforms remain `source_only`. Shared logical validation belongs to Null and
is never used to simulate Vulkan behavior.

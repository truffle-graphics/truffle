#include "consumer_convert.hpp"
#include "truffle/rhi/validation.hpp"
#include "window_rhi.hpp"

#if defined(TFL_PRESENTATION_METAL)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TFL_PRESENTATION_VULKAN)
#include "truffle/rhi/vulkan_backend.hpp"
#endif

namespace truffle::renderer {

base::Result<Device> Device::create(const DeviceOptions &options) {
  auto window = native_surface(options.presentationWindow);
  if (!window.ok())
    return window.status();
  if (!rhi::validation::native_surface_handles_valid(window.value()))
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "Native device setup requires valid window handles");
  auto backend = options.backend;
  if (backend == Backend::automatic) {
    if (options.presentationWindow.kind == WindowKind::cocoa_layer) {
#if defined(TFL_PRESENTATION_METAL)
      backend = Backend::metal;
#else
      backend = Backend::vulkan;
#endif
    } else if (options.presentationWindow.kind == WindowKind::headless) {
#if defined(__APPLE__) && defined(TFL_PRESENTATION_METAL)
      backend = Backend::metal;
#else
      backend = Backend::vulkan;
#endif
    } else if (options.presentationWindow.kind == WindowKind::win32 ||
               options.presentationWindow.kind == WindowKind::xcb) {
      backend = Backend::vulkan;
    } else
      return base::Status::failure(
          base::StatusCode::unsupported,
          "Automatic device setup does not support this window kind");
  }
  base::Result<rhi::Instance> instance = base::Status::failure(
      base::StatusCode::unsupported, "Requested native backend is not enabled");
  switch (backend) {
#if defined(TFL_PRESENTATION_METAL)
  case Backend::metal:
    instance = rhi::create_metal_instance({.enableValidation = options.enableValidation});
    break;
#endif
#if defined(TFL_PRESENTATION_VULKAN)
  case Backend::vulkan:
    instance =
        rhi::create_vulkan_instance({.enableValidation = options.enableValidation});
    break;
#endif
  default:
    break;
  }
  if (!instance.ok())
    return instance.status();
  const bool presentation = options.presentationWindow.kind != WindowKind::headless;
  rhi::DeviceDesc desc;
  if (presentation)
    desc.requiredFeatures.push_back(rhi::Feature::presentation);
  auto failure = base::Status::failure(base::StatusCode::unavailable,
                                       "No suitable graphics adapter");
  for (std::size_t index = 0; index < instance.value().adapter_count(); ++index) {
    auto adapter = instance.value().adapter(index);
    if (!adapter.ok())
      return adapter.status();
    if (!rhi::validation::supports_queue(adapter.value().info(),
                                         rhi::QueueKind::graphics) ||
        (presentation && !adapter.value().info().presentation))
      continue;
    auto device = adapter.value().request_device(desc);
    if (device.ok())
      return RhiInterop::adopt(std::move(instance).value(), std::move(device).value());
    failure = device.status();
    if (failure.code != base::StatusCode::unavailable &&
        failure.code != base::StatusCode::unsupported)
      return failure;
  }
  return failure;
}
base::Result<Presentation>
create_native_presentation(const PresentationDesc &desc,
                           const NativePresentationOptions &options) {
  if (desc.native.kind == WindowKind::headless && options.backend == Backend::automatic) {
    return base::Status::failure(base::StatusCode::unsupported,
                                 "Native presentation requires a host window");
  }
  auto device = Device::create({options.backend, options.enableValidation, desc.native});
  if (!device.ok())
    return device.status();
  return Presentation::create(device.value(), desc);
}

} // namespace truffle::renderer

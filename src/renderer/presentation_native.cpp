#include "truffle/renderer/presentation.hpp"

#if defined(TFL_PRESENTATION_METAL)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TFL_PRESENTATION_VULKAN)
#include "truffle/rhi/vulkan_backend.hpp"
#endif

namespace truffle::renderer {

base::Result<Presentation>
create_native_presentation(const PresentationDesc &desc,
                           const NativePresentationOptions &options) {
  auto backend = options.backend;
  if (!backend) {
    switch (desc.native.kind) {
    case rhi::NativeSurfaceKind::cocoa_layer:
#if defined(TFL_PRESENTATION_METAL)
      backend = rhi::BackendKind::metal;
#else
      backend = rhi::BackendKind::vulkan;
#endif
      break;
    case rhi::NativeSurfaceKind::win32:
    case rhi::NativeSurfaceKind::xcb:
      backend = rhi::BackendKind::vulkan;
      break;
    default:
      return base::Status::failure(
          base::StatusCode::unsupported,
          "Presentation: automatic setup requires CAMetalLayer, Win32 or XCB");
    }
  }
  base::Result<rhi::Instance> instance = base::Status::failure(
      base::StatusCode::unsupported,
      "Presentation: requested native backend is not linked; enable Metal or Vulkan");
  switch (*backend) {
#if defined(TFL_PRESENTATION_METAL)
  case rhi::BackendKind::metal:
    instance = rhi::create_metal_instance(options.instance);
    break;
#endif
#if defined(TFL_PRESENTATION_VULKAN)
  case rhi::BackendKind::vulkan:
    instance = rhi::create_vulkan_instance(options.instance);
    break;
#endif
  default:
    break;
  }
  if (!instance.ok()) {
    return instance.status();
  }
  return Presentation::create(std::move(instance).value(), desc, options.device);
}

} // namespace truffle::renderer

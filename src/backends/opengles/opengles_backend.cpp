#include "truffle/rhi/opengles_backend.hpp"

#include "foundation_backend.hpp"

#ifdef TRUFFLE_HAS_EGL_OPENGLES
#define TRUFFLE_EGL_API_OPENGLES 1
#include "egl_probe.hpp"
#endif

namespace truffle::rhi {

Result<Instance> create_opengles_instance(const InstanceDesc& desc) {
#ifdef TRUFFLE_HAS_EGL_OPENGLES
    auto probe = detail::egl_probe::initialize();
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::opengles;
    config.platform = PlatformKind::linux;
    config.maturity = BackendMaturity::native_smoke;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.nativeSubmit = &detail::egl_probe::submit;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengles, "OpenGL ES");
#endif
}

} // namespace truffle::rhi

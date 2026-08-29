#include "truffle/rhi/opengl_backend.hpp"

#include "foundation_backend.hpp"

#ifdef TRUFFLE_HAS_EGL_OPENGL
#define TRUFFLE_EGL_API_OPENGL 1
#include "egl_probe.hpp"
#endif

namespace truffle::rhi {

Result<Instance> create_opengl_instance(const InstanceDesc& desc) {
#ifdef TRUFFLE_HAS_EGL_OPENGL
    auto probe = detail::egl_probe::initialize();
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::opengl;
    config.platform = PlatformKind::linux_host;
    config.maturity = BackendMaturity::native_smoke;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.nativeSubmit = &detail::egl_probe::submit;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengl, "OpenGL");
#endif
}

} // namespace truffle::rhi

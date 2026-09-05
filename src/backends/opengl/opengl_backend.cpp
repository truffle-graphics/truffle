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
    config.supportedFeatures = {Feature::transfer};
    config.resourceCapabilities = {
        .bufferViews = true,
        .textureViews = false,
        .hostCoherent = true,
        .bufferCopy = true,
        .bufferFill = true,
        .bufferTextureCopy = true,
        .textureCopy = true,
        .textureClear = true,
        .textureBlitNearest = true,
        .textureBlitLinear = true,
    };
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.createBuffer = &detail::egl_probe::create_buffer;
    config.mapBuffer = &detail::egl_probe::map_buffer;
    config.unmapBuffer = &detail::egl_probe::unmap_buffer;
    config.flushBuffer = &detail::egl_probe::flush_buffer;
    config.invalidateBuffer = &detail::egl_probe::buffer_range;
    config.writeBuffer = &detail::egl_probe::write_buffer;
    config.readBuffer = &detail::egl_probe::read_buffer;
    config.createTexture = &detail::egl_probe::create_texture;
    config.writeTexture = &detail::egl_probe::write_texture;
    config.readTexture = &detail::egl_probe::read_texture;
    config.createSampler = &detail::egl_probe::create_sampler;
    config.nativeSubmit = &detail::egl_probe::submit;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengl, "OpenGL");
#endif
}

} // namespace truffle::rhi

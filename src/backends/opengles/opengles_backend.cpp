#include "truffle/rhi/opengles_backend.hpp"

#include "foundation_backend.hpp"

#ifdef TRUFFLE_HAS_EGL_OPENGLES
#define TRUFFLE_GL_API_ES 1
#include "gl_probe.hpp"
#endif

namespace truffle::rhi {

Result<Instance> create_opengles_instance(const InstanceDesc& desc) {
#ifdef TRUFFLE_HAS_EGL_OPENGLES
    auto probe = detail::gl_probe::initialize(desc);
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::opengles;
    config.platform = PlatformKind::linux_host;
    config.maturity = BackendMaturity::native_smoke;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
    config.supportedFeatures = {Feature::transfer, Feature::descriptor_arrays,
                                Feature::dynamic_offsets, Feature::push_constants};
    config.resourceCapabilities = {
        .bufferViews = true,
        .textureViews = true,
        .hostCoherent = true,
        .bufferCopy = true,
        .bufferFill = true,
        .bufferTextureCopy = true,
        .textureCopy = true,
        .textureClear = true,
        .textureBlitNearest = true,
        .textureBlitLinear = true,
    };
    config.bindingCapabilities = {
        .ordinaryBindGroups = true,
        .descriptorArrays = true,
        .dynamicOffsets = true,
        .immutableSamplers = true,
        .pushConstants = true,
        .maxBindGroups = 4,
        .maxBindingsPerGroup = 32,
        .maxDescriptorsPerGroup = 32,
        .maxPushConstantBytes = 128,
        .minUniformBufferOffsetAlignment = native.uniformAlignment,
    };
    config.pipelineCapabilities = {
        .graphics = true,
        .multipleRenderTargets = true,
        .depthStencil = true,
        .maxColorAttachments = 4,
        .maxVertexBuffers = 8,
        .maxViewports = 1,
    };
    config.native = true;
    config.nativeContext = std::move(native.context);
    config.createBuffer = &detail::gl_probe::create_buffer;
    config.mapBuffer = &detail::gl_probe::map_buffer;
    config.unmapBuffer = &detail::gl_probe::unmap_buffer;
    config.flushBuffer = &detail::gl_probe::flush_buffer;
    config.invalidateBuffer = &detail::gl_probe::buffer_range;
    config.writeBuffer = &detail::gl_probe::write_buffer;
    config.readBuffer = &detail::gl_probe::read_buffer;
    config.createTexture = &detail::gl_probe::create_texture;
    config.createTextureView = &detail::gl_probe::create_texture_view;
    config.writeTexture = &detail::gl_probe::write_texture;
    config.readTexture = &detail::gl_probe::read_texture;
    config.createSampler = &detail::gl_probe::create_sampler;
    config.createShader = &detail::gl_probe::create_shader;
    config.createPipeline = &detail::gl_probe::create_pipeline;
    config.createComputePipeline = &detail::gl_probe::create_compute_pipeline;
    config.nativeSubmit = &detail::gl_probe::submit;
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengles, "OpenGL ES");
#endif
}

} // namespace truffle::rhi

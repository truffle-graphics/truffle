#include "truffle/rhi/opengl_backend.hpp"

#include "foundation_backend.hpp"

#if defined(TFL_HAS_EGL_OPENGL) || defined(TFL_HAS_CGL_OPENGL) || \
    defined(TFL_HAS_WGL_OPENGL)
#define TFL_HAS_NATIVE_OPENGL 1
#define TFL_GL_API_DESKTOP 1
#if defined(TFL_HAS_CGL_OPENGL)
#define TFL_GL_CONTEXT_CGL 1
#elif defined(TFL_HAS_WGL_OPENGL)
#define TFL_GL_CONTEXT_WGL 1
#else
#define TFL_GL_CONTEXT_EGL 1
#endif
#include "gl_probe.hpp"
#endif
#if defined(TFL_GL_CONTEXT_CGL) || defined(TFL_GL_CONTEXT_WGL)
#include "gl_present.hpp"
#endif

namespace truffle::rhi {

#if defined(TFL_GL_CONTEXT_CGL) || defined(TFL_GL_CONTEXT_WGL)
namespace detail {

void set_opengl_present_capture_for_testing(bool enabled) noexcept {
    gl_probe::present_capture_state().enabled.store(enabled);
}

OpenGLPresentCapture opengl_last_present_capture_for_testing() {
    auto& capture = gl_probe::present_capture_state();
    std::lock_guard lock{capture.mutex};
    return capture.last;
}

#if defined(TFL_GL_CONTEXT_WGL)
void set_opengl_context_loss_for_testing(bool enabled) noexcept {
    gl_probe::contextLossForTesting.store(enabled);
}
#endif

} // namespace detail
#endif

Result<Instance> create_opengl_instance(const InstanceDesc& desc) {
#ifdef TFL_HAS_NATIVE_OPENGL
    auto probe = detail::gl_probe::initialize(desc);
    if (!probe.ok()) {
        return probe.status();
    }
    auto native = std::move(probe).value();
    detail::FoundationBackendConfig config;
    config.kind = BackendKind::opengl;
#if defined(TFL_GL_CONTEXT_CGL)
    config.platform = PlatformKind::macos;
#elif defined(TFL_GL_CONTEXT_WGL)
    config.platform = PlatformKind::windows;
#else
    config.platform = PlatformKind::linux_host;
#endif
    config.maturity = BackendMaturity::native_smoke;
    config.adapterName = std::move(native.adapterName);
    config.queueKinds = {QueueKind::graphics};
#if defined(TFL_GL_HAS_COMPUTE)
    config.supportedFeatures = {Feature::transfer, Feature::compute,
                                Feature::descriptor_arrays, Feature::dynamic_offsets,
                                Feature::push_constants};
#elif defined(TFL_GL_CONTEXT_CGL)
    config.supportedFeatures = {Feature::presentation, Feature::transfer,
                                Feature::descriptor_arrays,
                                Feature::dynamic_offsets, Feature::push_constants};
    config.presentation = true;
#else
    config.supportedFeatures = {Feature::transfer, Feature::descriptor_arrays,
                                Feature::dynamic_offsets, Feature::push_constants};
#endif
    // Presentation is independent of the compute feature tier.
#if defined(TFL_GL_CONTEXT_WGL)
    config.supportedFeatures.push_back(Feature::presentation);
    config.presentation = true;
#endif
    if (native.timestampQueries) {
        config.supportedFeatures.push_back(Feature::timestamp_queries);
        config.timestampPeriodNanoseconds = 1.0;
    }
    config.resourceCapabilities = {
        .bufferViews = true,
        .textureViews = native.textureViews,
        .hostCoherent = true,
        .bufferCopy = true,
        .bufferFill = true,
        .bufferTextureCopy = true,
        .textureCopy = true,
        .textureClear = true,
        .textureResolve = true,
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
        .minStorageBufferOffsetAlignment = native.storageAlignment,
    };
    config.pipelineCapabilities = {
        .graphics = true,
#if defined(TFL_GL_HAS_COMPUTE)
        .compute = true,
#else
        .compute = false,
#endif
        .multipleRenderTargets = true,
        .depthStencil = true,
        .multisample = true,
        .indirect = true,
        .maxColorAttachments = 4,
        .maxVertexBuffers = 8,
        .maxViewports = 1,
        .maxComputeWorkgroupSize = native.workgroupSize,
        .maxComputeInvocations = native.workgroupInvocations,
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
#if defined(TFL_GL_HAS_SYNC_OBJECTS)
    config.createSemaphore = &detail::gl_probe::create_semaphore;
    config.createQueryPool = &detail::gl_probe::create_query_pool;
#endif
#if defined(TFL_GL_CONTEXT_CGL) || defined(TFL_GL_CONTEXT_WGL)
    config.createSurface = &detail::gl_probe::create_surface;
    config.createSwapchain = &detail::gl_probe::create_swapchain;
    config.acquireSwapchain = &detail::gl_probe::acquire_swapchain;
    config.resizeSwapchain = &detail::gl_probe::resize_swapchain;
    config.presentSwapchain = &detail::gl_probe::present_swapchain;
#endif
    return detail::create_foundation_instance(desc, std::move(config));
#else
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengl, "OpenGL");
#endif
}

} // namespace truffle::rhi

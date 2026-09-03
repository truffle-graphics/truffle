#include "truffle/rhi/null_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_null_instance(const InstanceDesc& desc) {
    return detail::create_foundation_instance(
        desc,
            {
                .kind = BackendKind::null_validation,
                .platform = PlatformKind::all,
                .maturity = BackendMaturity::validation_only,
                .adapterName = "Truffle Null validation",
                .queueKinds = {QueueKind::graphics, QueueKind::compute,
                               QueueKind::transfer},
                .supportedFeatures = {Feature::presentation, Feature::compute,
                                      Feature::transfer, Feature::memory_budget,
                                      Feature::timestamp_queries,
                                      Feature::descriptor_arrays,
                                      Feature::dynamic_offsets,
                                      Feature::push_constants,
                                      Feature::indirect_count,
                                      Feature::pipeline_cache},
                .resourceCapabilities = {
                    .bufferViews = true,
                    .textureViews = true,
                    .hostCoherent = true,
                    .bufferCopy = true,
                    .bufferFill = true,
                    .bufferTextureCopy = true,
                    .textureCopy = true,
                    .textureClear = true,
                    .textureResolve = true,
                    .textureBlitNearest = true,
                    .textureBlitLinear = false,
                    .externalImport = false,
                    .externalExport = false,
                },
                .bindingCapabilities = {
                    .ordinaryBindGroups = true,
                    .descriptorArrays = true,
                    .dynamicOffsets = true,
                    .immutableSamplers = true,
                    .pushConstants = true,
                    .bindlessTables = false,
                    .updateAfterBind = false,
                    .maxBindGroups = 8,
                    .maxBindingsPerGroup = 64,
                    .maxDescriptorsPerGroup = 1024,
                    .maxPushConstantBytes = 256,
                    .minUniformBufferOffsetAlignment = 256,
                    .minStorageBufferOffsetAlignment = 16,
                },
                .pipelineCapabilities = {
                    .graphics = true,
                    .compute = true,
                    .multipleRenderTargets = true,
                    .depthStencil = true,
                    .multisample = true,
                    .tessellation = true,
                    .indirect = true,
                    .indirectCount = true,
                    .pipelineCache = true,
                    .maxColorAttachments = 8,
                    .maxVertexBuffers = 16,
                    .maxViewports = 16,
                    .maxComputeWorkgroupSize = {1024, 1024, 64},
                    .maxComputeInvocations = 1024,
                },
                .native = false,
            .validationOnly = true,
            .presentation = true,
            .logicalResources = true,
            .nativeContext = {},
        });
}

} // namespace truffle::rhi

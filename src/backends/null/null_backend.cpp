#include "truffle/rhi/null_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_null_instance(const InstanceDesc& desc) {
    return detail::create_foundation_instance(
        desc,
            {
                .kind = BackendKind::null_validation,
                .adapterName = "Truffle Null validation",
                .queueKinds = {QueueKind::graphics, QueueKind::compute,
                               QueueKind::transfer},
                .supportedFeatures = {Feature::presentation, Feature::compute,
                                      Feature::transfer, Feature::memory_budget},
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
                .native = false,
            .validationOnly = true,
            .presentation = true,
            .logicalResources = true,
        });
}

} // namespace truffle::rhi

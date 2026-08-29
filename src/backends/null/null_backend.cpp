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
                                      Feature::transfer},
                .native = false,
            .validationOnly = true,
            .presentation = true,
            .logicalResources = true,
        });
}

} // namespace truffle::rhi

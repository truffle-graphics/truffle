#pragma once

#include "truffle/rhi/rhi.hpp"

#include <string>

namespace truffle::rhi::detail {

struct FoundationBackendConfig {
    BackendKind kind = BackendKind::null_validation;
    std::string adapterName;
    std::vector<QueueKind> queueKinds;
    std::vector<Feature> supportedFeatures;
    bool native = false;
    bool validationOnly = false;
    bool presentation = false;
    bool logicalResources = false;
    Status (*nativeSubmit)() = nullptr;
};

[[nodiscard]] Result<Instance> create_foundation_instance(
    const InstanceDesc& desc, FoundationBackendConfig config);

[[nodiscard]] Result<Instance> unavailable_backend(
    BackendKind kind, std::string backendName);

} // namespace truffle::rhi::detail

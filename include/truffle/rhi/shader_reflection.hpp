#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace truffle::rhi {

/// Represents the high-level category of an expected shader resource.
enum class ResourceBindingType {
    Buffer,
    Texture,
    Sampler,
    Unknown
};

/// Represents a single resource binding described by pipeline reflection.
struct ResourceBinding {
    std::string         name;
    ShaderStage         stage;
    ResourceBindingType type;
    std::uint32_t       bindingIndex;
    std::size_t         dataSize = 0;
};

/// Provides agnostic discovery of resources required by a compiled pipeline.
class IPipelineReflection {
public:
    virtual ~IPipelineReflection() = default;

    /// Return the total number of resource bindings discovered.
    [[nodiscard]] virtual std::size_t
    get_binding_count() const noexcept = 0;

    /// Return details about the binding at the given index.
    [[nodiscard]] virtual const ResourceBinding&
    get_binding_info(std::size_t index) const = 0;
};

} // namespace truffle::rhi

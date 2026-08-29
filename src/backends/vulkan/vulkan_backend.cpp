#include "truffle/rhi/vulkan_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_vulkan_instance(const InstanceDesc& desc) {
    (void)desc;
    return detail::unavailable_backend(BackendKind::vulkan, "Vulkan");
}

} // namespace truffle::rhi

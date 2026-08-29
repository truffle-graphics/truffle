#include "truffle/rhi/webgpu_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_webgpu_instance(const InstanceDesc& desc) {
    (void)desc;
    return detail::unavailable_backend(BackendKind::webgpu, "WebGPU");
}

} // namespace truffle::rhi

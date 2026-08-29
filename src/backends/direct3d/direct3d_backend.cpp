#include "truffle/rhi/direct3d_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_direct3d12_instance(const InstanceDesc& desc) {
    (void)desc;
    return detail::unavailable_backend(BackendKind::direct3d12, "Direct3D 12");
}

} // namespace truffle::rhi

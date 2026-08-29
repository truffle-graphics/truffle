#include "truffle/rhi/opengl_backend.hpp"

#include "foundation_backend.hpp"

namespace truffle::rhi {

Result<Instance> create_opengl_instance(const InstanceDesc& desc) {
    (void)desc;
    return detail::unavailable_backend(BackendKind::opengl, "OpenGL");
}

} // namespace truffle::rhi

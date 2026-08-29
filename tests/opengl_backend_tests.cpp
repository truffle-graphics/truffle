#include "truffle/rhi/opengl_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#ifdef TRUFFLE_EXPECT_EGL_OPENGL
    truffle::tests::verify_native_buffer_backend(
        truffle::rhi::create_opengl_instance(),
        truffle::rhi::BackendKind::opengl,
        truffle::rhi::PlatformKind::linux_host);
#else
    const auto result = truffle::rhi::create_opengl_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

#include "truffle/rhi/opengles_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#ifdef TRUFFLE_EXPECT_EGL_OPENGLES
    truffle::tests::verify_native_backend_smoke(
        truffle::rhi::create_opengles_instance(),
        truffle::rhi::BackendKind::opengles,
        truffle::rhi::PlatformKind::linux_host);
#else
    const auto result = truffle::rhi::create_opengles_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

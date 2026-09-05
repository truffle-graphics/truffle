#include "truffle/rhi/opengles_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#ifdef TRUFFLE_EXPECT_EGL_OPENGLES
    std::size_t diagnostics = 0;
    truffle::tests::verify_native_buffer_backend(
        truffle::rhi::create_opengles_instance(),
        truffle::rhi::BackendKind::opengles,
        truffle::rhi::PlatformKind::linux_host);
    truffle::tests::verify_native_texture_backend(
        truffle::rhi::create_opengles_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengles,
        truffle::rhi::PlatformKind::linux_host, &diagnostics);
    truffle::tests::verify_native_gl_graphics(
        truffle::rhi::create_opengles_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengles,
        truffle::rhi::ShaderTarget::glsl_es, &diagnostics);
#else
    const auto result = truffle::rhi::create_opengles_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

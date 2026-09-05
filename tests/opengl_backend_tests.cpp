#include "truffle/rhi/opengl_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#ifdef TRUFFLE_EXPECT_EGL_OPENGL
    std::size_t diagnostics = 0;
    truffle::tests::verify_native_buffer_backend(
        truffle::rhi::create_opengl_instance(),
        truffle::rhi::BackendKind::opengl,
        truffle::rhi::PlatformKind::linux_host);
    truffle::tests::verify_native_texture_backend(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengl,
        truffle::rhi::PlatformKind::linux_host, &diagnostics);
    truffle::tests::verify_native_gl_graphics(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengl,
        truffle::rhi::ShaderTarget::glsl, &diagnostics);
#else
    const auto result = truffle::rhi::create_opengl_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

#include "truffle/rhi/opengl_backend.hpp"

#include "native_backend_smoke.hpp"
#include "gl_graphics_tests.hpp"
#include "gl_binding_tests.hpp"
#include "gl_sync_tests.hpp"

int main() {
#if defined(TRUFFLE_EXPECT_EGL_OPENGL) || defined(TRUFFLE_EXPECT_CGL_OPENGL)
#if defined(TRUFFLE_EXPECT_CGL_OPENGL)
    constexpr auto platform = truffle::rhi::PlatformKind::macos;
#else
    constexpr auto platform = truffle::rhi::PlatformKind::linux_host;
#endif
    std::size_t diagnostics = 0;
    truffle::tests::verify_native_buffer_backend(
        truffle::rhi::create_opengl_instance(),
        truffle::rhi::BackendKind::opengl,
        platform);
    truffle::tests::verify_native_texture_backend(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengl,
        platform, &diagnostics);
    truffle::tests::verify_native_gl_graphics(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        }),
        truffle::rhi::BackendKind::opengl,
        truffle::rhi::ShaderTarget::glsl, &diagnostics);
    truffle::tests::verify_expanded_gl_graphics(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics}), true, &diagnostics);
    truffle::tests::verify_gl_bindings(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics}), true, &diagnostics);
    truffle::tests::verify_gl_compute(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics}), true, &diagnostics);
#if defined(TRUFFLE_EXPECT_CGL_OPENGL)
    constexpr bool expectSync = true;
#else
    constexpr bool expectSync = false;
#endif
    truffle::tests::verify_gl_sync(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics}), expectSync, &diagnostics);
#else
    const auto result = truffle::rhi::create_opengl_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

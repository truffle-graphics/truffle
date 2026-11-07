#include "truffle/rhi/opengl_backend.hpp"

#include "native_backend_smoke.hpp"
#include "gl_graphics_tests.hpp"
#include "gl_binding_tests.hpp"
#include "gl_sync_tests.hpp"

#if defined(TFL_EXPECT_WGL_OPENGL)
#include <windows.h>
#include <crtdbg.h>
#include <cstdio>
#endif

int main() {
#if defined(TFL_EXPECT_WGL_OPENGL)
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    {
        auto probe = truffle::rhi::create_opengl_instance({.enableValidation = true});
        if (!probe.ok()) {
            std::fprintf(stderr, "WGL initialization failed: %s\n",
                         probe.status().message.c_str());
            return 1;
        }
        auto adapter = probe.value().adapter(0);
        assert(adapter.ok());
        std::printf("WGL adapter: %s\n", adapter.value().info().name.c_str());
        assert(adapter.value().info().name.find("[KHR_debug]") != std::string::npos);
    }
#endif
#if defined(TFL_EXPECT_EGL_OPENGL) || defined(TFL_EXPECT_CGL_OPENGL) || \
    defined(TFL_EXPECT_WGL_OPENGL)
#if defined(TFL_EXPECT_CGL_OPENGL)
    constexpr auto platform = truffle::rhi::PlatformKind::macos;
#elif defined(TFL_EXPECT_WGL_OPENGL)
    constexpr auto platform = truffle::rhi::PlatformKind::windows;
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
#if defined(TFL_EXPECT_CGL_OPENGL) || defined(TFL_EXPECT_WGL_OPENGL)
    constexpr bool expectSync = true;
#else
    constexpr bool expectSync = false;
#endif
    truffle::tests::verify_gl_sync(
        truffle::rhi::create_opengl_instance({
            .debugCallback = &truffle::tests::count_native_diagnostic,
            .debugUserData = &diagnostics}), expectSync, &diagnostics,
#if defined(TFL_EXPECT_WGL_OPENGL)
        true);
#else
        false);
#endif
#else
    const auto result = truffle::rhi::create_opengl_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

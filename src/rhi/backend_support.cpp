#include "truffle/rhi/backend_support.hpp"

#include <array>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace truffle::rhi {
namespace {

constexpr BackendEvidence source_only{};
constexpr BackendEvidence validated_native_smoke{
    .compiles = true,
    .nativeSmoke = true,
    .validation = true,
};
constexpr BackendEvidence metal_smoke{
    .compiles = true,
    .nativeSmoke = true,
    .validation = true,
    .presentation = true,
};
constexpr BackendEvidence validation_only{
    .compiles = true,
    .conformance = true,
    .validation = true,
    .presentation = true,
};

constexpr std::array support{
    BackendPlatformSupport{BackendKind::null_validation, PlatformKind::all,
                           BackendMaturity::validation_only, false,
                           validation_only,
                           "strict contract interpreter; never a GPU adapter"},
    BackendPlatformSupport{BackendKind::metal, PlatformKind::macos,
                           BackendMaturity::native_smoke, true, metal_smoke,
                           "native output, synchronization, and CAMetalLayer proof"},
    BackendPlatformSupport{BackendKind::metal, PlatformKind::ios,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no device or simulator lane yet"},
    BackendPlatformSupport{BackendKind::metal, PlatformKind::ipados,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no device or simulator lane yet"},
    BackendPlatformSupport{BackendKind::metal, PlatformKind::tvos,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no device or simulator lane yet"},
    BackendPlatformSupport{BackendKind::metal, PlatformKind::visionos,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no device or simulator lane yet"},
    BackendPlatformSupport{BackendKind::vulkan, PlatformKind::linux_host,
                           BackendMaturity::native_smoke, true,
                           validated_native_smoke,
                           "native buffers and selected 2D texture transfers "
                           "using bundled headers and volk"},
    BackendPlatformSupport{BackendKind::vulkan, PlatformKind::windows,
                           BackendMaturity::source_only, true, source_only,
                           "native loader source is present; no official runtime lane yet"},
    BackendPlatformSupport{BackendKind::vulkan, PlatformKind::android,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no NDK lane yet"},
    BackendPlatformSupport{BackendKind::vulkan, PlatformKind::macos,
                           BackendMaturity::source_only, true, source_only,
                           "MoltenVK dependency and execution lane not present"},
    BackendPlatformSupport{BackendKind::vulkan, PlatformKind::ios,
                           BackendMaturity::source_only, true, source_only,
                           "MoltenVK dependency and execution lane not present"},
    BackendPlatformSupport{BackendKind::direct3d12, PlatformKind::windows,
                           BackendMaturity::native_smoke, true,
                           validated_native_smoke,
                           "Windows SDK WARP buffers, views, copies, and byte "
                           "fills"},
    BackendPlatformSupport{BackendKind::opengl, PlatformKind::linux_host,
                           BackendMaturity::native_smoke, true,
                           validated_native_smoke,
                           "EGL surfaceless buffers, views, copies, fills, and "
                           "exact readback"},
    BackendPlatformSupport{BackendKind::opengl, PlatformKind::windows,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no WGL execution lane yet"},
    BackendPlatformSupport{BackendKind::opengl, PlatformKind::macos,
                           BackendMaturity::source_only, true, source_only,
                           "deprecated compatibility target; no native lane yet"},
    BackendPlatformSupport{BackendKind::opengles, PlatformKind::linux_host,
                           BackendMaturity::native_smoke, true,
                           validated_native_smoke,
                           "EGL ES 3 surfaceless buffers, views, copies, fills, "
                           "and exact readback"},
    BackendPlatformSupport{BackendKind::opengles, PlatformKind::android,
                           BackendMaturity::source_only, true, source_only,
                           "target identified; no NDK execution lane yet"},
    BackendPlatformSupport{BackendKind::webgpu, PlatformKind::web,
                           BackendMaturity::source_only, true, source_only,
                           "Emdawnwebgpu package and browser lane not present"},
    BackendPlatformSupport{BackendKind::webgl2, PlatformKind::web,
                           BackendMaturity::source_only, true, source_only,
                           "browser target exists without execution evidence"},
};

} // namespace

std::string_view backend_name(BackendKind backend) noexcept {
    switch (backend) {
    case BackendKind::null_validation:
        return "null";
    case BackendKind::metal:
        return "metal";
    case BackendKind::vulkan:
        return "vulkan";
    case BackendKind::direct3d12:
        return "direct3d12";
    case BackendKind::opengl:
        return "opengl";
    case BackendKind::webgpu:
        return "webgpu";
    case BackendKind::opengles:
        return "opengles";
    case BackendKind::webgl2:
        return "webgl2";
    }
    return "unknown";
}

std::string_view platform_name(PlatformKind platform) noexcept {
    switch (platform) {
    case PlatformKind::all:
        return "all";
    case PlatformKind::macos:
        return "macos";
    case PlatformKind::ios:
        return "ios";
    case PlatformKind::ipados:
        return "ipados";
    case PlatformKind::tvos:
        return "tvos";
    case PlatformKind::visionos:
        return "visionos";
    case PlatformKind::windows:
        return "windows";
    case PlatformKind::linux_host:
        return "linux";
    case PlatformKind::android:
        return "android";
    case PlatformKind::web:
        return "web";
    }
    return "unknown";
}

std::string_view maturity_name(BackendMaturity maturity) noexcept {
    switch (maturity) {
    case BackendMaturity::source_only:
        return "source_only";
    case BackendMaturity::cross_compiles:
        return "cross_compiles";
    case BackendMaturity::native_smoke:
        return "native_smoke";
    case BackendMaturity::conformant:
        return "conformant";
    case BackendMaturity::supported:
        return "supported";
    case BackendMaturity::validation_only:
        return "validation_only";
    }
    return "unknown";
}

PlatformKind host_platform() noexcept {
#if defined(__EMSCRIPTEN__)
    return PlatformKind::web;
#elif defined(__ANDROID__)
    return PlatformKind::android;
#elif defined(_WIN32)
    return PlatformKind::windows;
#elif defined(__APPLE__)
#if defined(TARGET_OS_VISION) && TARGET_OS_VISION
    return PlatformKind::visionos;
#elif TARGET_OS_TV
    return PlatformKind::tvos;
#elif TARGET_OS_IOS
    return PlatformKind::ios;
#else
    return PlatformKind::macos;
#endif
#elif defined(__linux__)
    return PlatformKind::linux_host;
#else
    return PlatformKind::all;
#endif
}

std::span<const BackendPlatformSupport> backend_platform_support() noexcept {
    return support;
}

} // namespace truffle::rhi

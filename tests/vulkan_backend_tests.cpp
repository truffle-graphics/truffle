#include "truffle/rhi/vulkan_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#if defined(__linux__) && !defined(__ANDROID__)
    truffle::tests::verify_native_backend_smoke(
        truffle::rhi::create_vulkan_instance(),
        truffle::rhi::BackendKind::vulkan,
        truffle::rhi::PlatformKind::linux_host);
#else
    auto result = truffle::rhi::create_vulkan_instance();
    if (result.ok()) {
        auto instance = std::move(result).value();
        auto adapter = instance.adapter(0);
        assert(adapter.ok());
        assert(adapter.value().info().native);
        assert(adapter.value().info().backend ==
               truffle::rhi::BackendKind::vulkan);
    } else {
        truffle::tests::verify_unavailable_backend(result);
    }
#endif
    return 0;
}

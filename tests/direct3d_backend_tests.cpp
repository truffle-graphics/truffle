#include "truffle/rhi/direct3d_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
#ifdef _WIN32
    truffle::tests::verify_native_backend_smoke(
        truffle::rhi::create_direct3d12_instance(),
        truffle::rhi::BackendKind::direct3d12,
        truffle::rhi::PlatformKind::windows);
#else
    const auto result = truffle::rhi::create_direct3d12_instance();
    truffle::tests::verify_unavailable_backend(result);
#endif
    return 0;
}

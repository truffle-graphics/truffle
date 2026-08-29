#include "truffle/rhi/webgpu_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
    const auto result = truffle::rhi::create_webgpu_instance();
    truffle::tests::verify_unavailable_backend(result);
    return 0;
}

#include "truffle/rhi/webgl2_backend.hpp"

#include "native_backend_smoke.hpp"

int main() {
    const auto result = truffle::rhi::create_webgl2_instance();
    if (!result.ok()) {
        truffle::tests::verify_unavailable_backend(result);
    }
    return 0;
}

#include "truffle/rhi/direct3d_backend.hpp"

#include <cassert>

int main() {
    const auto result = truffle::rhi::create_direct3d12_instance();
    assert(!result.ok());
    assert(result.status().code == truffle::core::StatusCode::unsupported);
    return 0;
}

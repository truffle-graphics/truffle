#include "truffle/rhi/vulkan_backend.hpp"

#include <cassert>

int main() {
    const auto result = truffle::rhi::create_vulkan_instance();
    assert(!result.ok());
    assert(result.status().code == truffle::core::StatusCode::unsupported);
    return 0;
}

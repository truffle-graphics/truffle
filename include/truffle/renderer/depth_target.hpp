#pragma once

#include "truffle/rhi/rhi.hpp"

namespace truffle::renderer {

// Single-thread-owned allocation helper. Device outlives it. Views borrow the
// wrapper only until resize(); recorded RHI payloads retain the old allocation.
class DepthTarget {
public:
    explicit DepthTarget(rhi::Device& device) : device_(&device) {}
    [[nodiscard]] base::Status resize(rhi::Extent2D extent,
                                      rhi::TextureFormat format = rhi::TextureFormat::depth32_float,
                                      std::uint32_t samples = 1);
    [[nodiscard]] rhi::Texture* texture() noexcept { return &texture_; }

private:
    rhi::Device* device_;
    rhi::Texture texture_;
};

} // namespace truffle::renderer

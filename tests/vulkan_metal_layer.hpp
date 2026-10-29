#pragma once

#include <cstdint>

namespace truffle::tests {

// Host-owned CAMetalLayer for Vulkan VK_EXT_metal_surface tests. The handle is
// an opaque CAMetalLayer pointer so the C++ test does not import Objective-C.
class VulkanMetalTestLayer {
public:
    VulkanMetalTestLayer(std::uint32_t width, std::uint32_t height);
    ~VulkanMetalTestLayer();

    VulkanMetalTestLayer(const VulkanMetalTestLayer&) = delete;
    VulkanMetalTestLayer& operator=(const VulkanMetalTestLayer&) = delete;

    [[nodiscard]] void* layer() const noexcept { return layer_; }

    void resize(std::uint32_t width, std::uint32_t height);

private:
    void* layer_ = nullptr;
};

} // namespace truffle::tests

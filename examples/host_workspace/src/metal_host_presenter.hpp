#pragma once

#include "truffle/base/status.hpp"
#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <string>

namespace truffle::examples::host_workspace {

struct MetalHostPresenterStats {
    std::uint32_t framesPresented = 0;
    std::uint32_t swapchainResizes = 0;
    std::uint32_t suboptimalAcquires = 0;
    std::uint32_t outOfDateAcquires = 0;
};

// Presents clear-colour frames through Truffle's Metal RHI into a CAMetalLayer
// owned by the host window. The host keeps window and layer lifecycle; this
// class owns only the Truffle device, swapchain, and per-frame submission.
class MetalHostPresenter {
public:
    [[nodiscard]] base::Status initialize(void* metalLayer,
                                          rhi::Extent2D extent);
    [[nodiscard]] base::Status present_frame(rhi::Extent2D framebufferExtent,
                                             rhi::ClearColor clear);

    [[nodiscard]] const std::string& adapter_name() const noexcept {
        return adapterName_;
    }
    [[nodiscard]] rhi::Extent2D extent() const noexcept { return extent_; }
    [[nodiscard]] const MetalHostPresenterStats& stats() const noexcept {
        return stats_;
    }

private:
    [[nodiscard]] base::Status resize(rhi::Extent2D extent);
    [[nodiscard]] base::Status record_clear(rhi::CommandList& list,
                                            rhi::Texture& image,
                                            rhi::ClearColor clear);

    rhi::Instance instance_;
    rhi::Adapter adapter_;
    rhi::Device device_;
    rhi::Surface surface_;
    rhi::Swapchain swapchain_;
    rhi::Queue queue_;
    rhi::CommandPool pool_;
    rhi::Semaphore rendered_;
    rhi::Fence frameFence_;
    std::uint64_t frameValue_ = 0;
    std::string adapterName_;
    rhi::Extent2D extent_;
    MetalHostPresenterStats stats_;
};

} // namespace truffle::examples::host_workspace

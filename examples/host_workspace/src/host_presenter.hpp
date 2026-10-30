#pragma once

#include "truffle/base/status.hpp"
#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <string>

namespace truffle::examples::host_workspace {

struct HostPresenterStats {
    std::uint32_t framesPresented = 0;
    std::uint32_t swapchainResizes = 0;
    std::uint32_t suboptimalAcquires = 0;
    std::uint32_t suboptimalPresents = 0;
    std::uint32_t outOfDateAcquires = 0;
};

// Presents clear-colour frames through a Truffle RHI instance into a native
// surface owned by the host window (a CAMetalLayer for Metal, an NSView for
// macOS OpenGL). The host keeps window, view and layer lifecycle; this class
// owns only the Truffle device, swapchain, and per-frame submission.
class HostPresenter {
public:
    [[nodiscard]] base::Status initialize(rhi::Instance instance,
                                          rhi::NativeSurface native,
                                          rhi::TextureFormat format,
                                          rhi::Extent2D extent);
    [[nodiscard]] base::Status present_frame(rhi::Extent2D framebufferExtent,
                                             rhi::ClearColor clear);

    [[nodiscard]] const std::string& adapter_name() const noexcept {
        return adapterName_;
    }
    [[nodiscard]] rhi::Extent2D extent() const noexcept { return extent_; }
    [[nodiscard]] const HostPresenterStats& stats() const noexcept {
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
    HostPresenterStats stats_;
};

} // namespace truffle::examples::host_workspace

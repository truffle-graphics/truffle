#include "host_presenter.hpp"

#include <array>
#include <chrono>
#include <utility>

namespace truffle::examples::host_workspace {
namespace {

constexpr auto kFrameTimeout = std::chrono::seconds{2};

[[nodiscard]] bool same_extent(rhi::Extent2D lhs, rhi::Extent2D rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

} // namespace

base::Status HostPresenter::initialize(rhi::Instance instance,
                                       rhi::NativeSurface native,
                                       rhi::TextureFormat format,
                                       rhi::Extent2D extent) {
    if (native.handle == nullptr || extent.width == 0 || extent.height == 0) {
        return base::Status::failure(
            base::StatusCode::invalid_argument,
            "host presenter needs a native surface and non-zero extent");
    }
    instance_ = std::move(instance);
    auto adapterResult = instance_.adapter(0);
    if (!adapterResult.ok()) {
        return adapterResult.status();
    }
    adapter_ = std::move(adapterResult).value();
    adapterName_ = adapter_.info().name;
    auto deviceResult = adapter_.request_device({
        .requiredFeatures = {rhi::Feature::presentation},
    });
    if (!deviceResult.ok()) {
        return deviceResult.status();
    }
    device_ = std::move(deviceResult).value();

    auto surfaceResult = device_.create_surface({
        .native = native,
        .initialExtent = extent,
        .debugName = "host window surface",
    });
    if (!surfaceResult.ok()) {
        return surfaceResult.status();
    }
    surface_ = std::move(surfaceResult).value();
    auto swapchainResult = device_.create_swapchain(
        surface_, {.extent = extent,
                   .format = format,
                   .presentMode = rhi::PresentMode::fifo,
                   .imageCount = 2,
                   .debugName = "host window swapchain"});
    if (!swapchainResult.ok()) {
        return swapchainResult.status();
    }
    swapchain_ = std::move(swapchainResult).value();

    auto queueResult = device_.queue(rhi::QueueKind::graphics);
    if (!queueResult.ok()) {
        return queueResult.status();
    }
    queue_ = std::move(queueResult).value();
    auto poolResult = device_.create_command_pool(rhi::QueueKind::graphics);
    if (!poolResult.ok()) {
        return poolResult.status();
    }
    pool_ = std::move(poolResult).value();
    auto renderedResult = device_.create_semaphore();
    if (!renderedResult.ok()) {
        return renderedResult.status();
    }
    rendered_ = std::move(renderedResult).value();
    auto fenceResult = device_.create_fence();
    if (!fenceResult.ok()) {
        return fenceResult.status();
    }
    frameFence_ = std::move(fenceResult).value();
    extent_ = extent;
    return base::Status::success();
}

base::Status HostPresenter::resize(rhi::Extent2D extent) {
    if (auto status = swapchain_.resize(extent); !status.ok()) {
        return status;
    }
    extent_ = extent;
    ++stats_.swapchainResizes;
    return base::Status::success();
}

base::Status HostPresenter::record_clear(rhi::CommandList& list,
                                              rhi::Texture& image,
                                              rhi::ClearColor clear) {
    if (auto status = list.begin(); !status.ok()) {
        return status;
    }
    rhi::BarrierBatch toRender;
    toRender.textures.push_back({
        .texture = &image,
        .oldLayout = rhi::TextureLayout::present,
        .newLayout = rhi::TextureLayout::color_attachment,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::color_attachment_output,
        .destinationAccess = rhi::Access::color_attachment_write,
    });
    if (auto status = list.barrier(toRender); !status.ok()) {
        return status;
    }
    const auto imageExtent = image.desc().extent;
    auto renderResult = list.begin_rendering({
        .extent = {imageExtent.width, imageExtent.height},
        .colorAttachments = {{.texture = &image, .clear = clear}},
    });
    if (!renderResult.ok()) {
        return renderResult.status();
    }
    auto render = std::move(renderResult).value();
    if (auto status = render.end(); !status.ok()) {
        return status;
    }
    rhi::BarrierBatch toPresent;
    toPresent.textures.push_back({
        .texture = &image,
        .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::present,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::bottom,
        .sourceAccess = rhi::Access::color_attachment_write,
    });
    if (auto status = list.barrier(toPresent); !status.ok()) {
        return status;
    }
    return list.end();
}

base::Status HostPresenter::present_frame(rhi::Extent2D framebufferExtent,
                                               rhi::ClearColor clear) {
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "framebuffer extent must be non-zero");
    }
    if (!same_extent(extent_, framebufferExtent)) {
        if (auto status = resize(framebufferExtent); !status.ok()) {
            return status;
        }
    }

    auto acquired = swapchain_.acquire_next_image();
    if (acquired.status.code == base::StatusCode::out_of_date) {
        ++stats_.outOfDateAcquires;
        if (auto status = resize(framebufferExtent); !status.ok()) {
            return status;
        }
        acquired = swapchain_.acquire_next_image();
    }
    if (!acquired.ok()) {
        return acquired.status;
    }
    const bool suboptimal =
        acquired.status.code == base::StatusCode::suboptimal;

    auto listResult = pool_.allocate();
    if (!listResult.ok()) {
        return listResult.status();
    }
    auto list = std::move(listResult).value();
    if (auto status = record_clear(list, *acquired.image, clear);
        !status.ok()) {
        return status;
    }

    ++frameValue_;
    std::array<rhi::CommandList*, 1> lists{&list};
    const std::array<rhi::SemaphoreWait, 1> acquireWaits{{
        {.semaphore = acquired.available,
         .value = acquired.availableValue,
         .stages = rhi::PipelineStage::color_attachment_output},
    }};
    const std::array<rhi::SemaphoreSignal, 1> renderedSignals{{
        {.semaphore = &rendered_, .value = frameValue_},
    }};
    if (auto status = queue_.submit({.commandLists = lists,
                                     .waits = acquireWaits,
                                     .signals = renderedSignals,
                                     .signalFence = &frameFence_,
                                     .signalFenceValue = frameValue_});
        !status.ok()) {
        return status;
    }
    const std::array<rhi::SemaphoreWait, 1> presentWaits{{
        {.semaphore = &rendered_,
         .value = frameValue_,
         .stages = rhi::PipelineStage::bottom},
    }};
    // A suboptimal present still reached the window (macOS OpenGL scales a
    // stale-sized image into the resized view); resize before the next frame.
    auto presented = queue_.present({.swapchain = &swapchain_,
                                     .imageIndex = acquired.imageIndex,
                                     .waits = presentWaits});
    if (presented.code == base::StatusCode::suboptimal) {
        ++stats_.suboptimalPresents;
    } else if (!presented.ok()) {
        return presented;
    }
    // The command list is released at scope exit, so wait for its work.
    if (auto status = frameFence_.wait(frameValue_, kFrameTimeout);
        !status.ok()) {
        return status;
    }
    ++stats_.framesPresented;

    if (suboptimal) {
        ++stats_.suboptimalAcquires;
    }
    if (suboptimal || presented.code == base::StatusCode::suboptimal) {
        if (auto status = resize(framebufferExtent); !status.ok()) {
            return status;
        }
    }
    return base::Status::success();
}

} // namespace truffle::examples::host_workspace

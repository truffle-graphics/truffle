#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "truffle/rhi/metal_backend.hpp"

#include "metal_backend_test.hpp"

#include <array>
#include <cassert>
#include <utility>

int main() {
    using namespace truffle;
    @autoreleasepool {
        auto instanceResult = rhi::create_metal_instance();
        if (!instanceResult.ok()) {
            assert(instanceResult.status().code == rhi::StatusCode::unavailable);
            return 0;
        }
        auto instance = std::move(instanceResult).value();
        auto adapterResult = instance.adapter(0);
        assert(adapterResult.ok());
        auto adapter = std::move(adapterResult).value();
        auto deviceResult = adapter.request_device({
            .requiredFeatures = {rhi::Feature::presentation},
        });
        assert(deviceResult.ok());
        auto device = std::move(deviceResult).value();

        CAMetalLayer* layer = [CAMetalLayer layer];
        assert(layer != nil);
        auto surfaceResult = device.create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                       .handle = (__bridge void*)layer},
            .initialExtent = {32, 24},
            .debugName = "Metal native presentation surface",
        });
        assert(surfaceResult.ok());
        auto surface = std::move(surfaceResult).value();
        auto swapchainResult = device.create_swapchain(
            surface, {.extent = {32, 24},
                      .format = rhi::TextureFormat::bgra8_unorm,
                      .presentMode = rhi::PresentMode::fifo,
                      .imageCount = 2,
                      .debugName = "Metal native presentation"});
        assert(swapchainResult.ok());
        auto swapchain = std::move(swapchainResult).value();
        auto queueResult = device.queue(rhi::QueueKind::graphics);
        auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
        auto renderedResult = device.create_semaphore();
        assert(queueResult.ok() && poolResult.ok() && renderedResult.ok());
        auto queue = std::move(queueResult).value();
        auto pool = std::move(poolResult).value();
        auto rendered = std::move(renderedResult).value();

        auto acquired = swapchain.acquire_next_image();
        assert(acquired.ok());
        auto listResult = pool.allocate();
        assert(listResult.ok());
        auto list = std::move(listResult).value();
        assert(list.begin().ok());
        rhi::BarrierBatch toRender;
        toRender.textures.push_back({
            .texture = acquired.image,
            .oldLayout = rhi::TextureLayout::present,
            .newLayout = rhi::TextureLayout::color_attachment,
            .sourceStages = rhi::PipelineStage::top,
            .destinationStages = rhi::PipelineStage::color_attachment_output,
            .destinationAccess = rhi::Access::color_attachment_write,
        });
        assert(list.barrier(toRender).ok());
        auto renderResult = list.begin_rendering({
            .extent = {32, 24},
            .colorAttachments = {{.texture = acquired.image,
                                  .clear = {0.1F, 0.2F, 0.4F, 1.0F}}},
        });
        assert(renderResult.ok());
        auto render = std::move(renderResult).value();
        assert(render.end().ok());
        rhi::BarrierBatch toPresent;
        toPresent.textures.push_back({
            .texture = acquired.image,
            .oldLayout = rhi::TextureLayout::color_attachment,
            .newLayout = rhi::TextureLayout::present,
            .sourceStages = rhi::PipelineStage::color_attachment_output,
            .destinationStages = rhi::PipelineStage::bottom,
            .sourceAccess = rhi::Access::color_attachment_write,
        });
        assert(list.barrier(toPresent).ok());
        assert(list.end().ok());
        std::array<rhi::CommandList*, 1> lists{&list};
        const std::array<rhi::SemaphoreWait, 1> acquireWaits{{
            {.semaphore = acquired.available,
             .value = acquired.availableValue,
             .stages = rhi::PipelineStage::color_attachment_output},
        }};
        const std::array<rhi::SemaphoreSignal, 1> renderedSignals{{
            {.semaphore = &rendered, .value = 1},
        }};
        assert(queue
                   .submit({.commandLists = lists,
                            .waits = acquireWaits,
                            .signals = renderedSignals})
                   .ok());
        const std::array<rhi::SemaphoreWait, 1> presentWaits{{
            {.semaphore = &rendered,
             .value = 1,
             .stages = rhi::PipelineStage::bottom},
        }};
        assert(queue
                   .present({.swapchain = &swapchain,
                             .imageIndex = acquired.imageIndex,
                             .waits = presentWaits})
                   .ok());

        // A drawable-size drift remains usable but is reported as suboptimal.
        layer.drawableSize = CGSizeMake(40, 30);
        auto suboptimal = swapchain.acquire_next_image();
        assert(suboptimal.ok());
        assert(suboptimal.status.code == rhi::StatusCode::suboptimal);
        assert(queue.present(swapchain, suboptimal.imageIndex).ok());

        // A zero-sized layer is out of date; resize is the recovery operation.
        rhi::detail::set_metal_acquire_fault_for_testing(
            rhi::detail::MetalAcquireFault::out_of_date);
        const auto outOfDate = swapchain.acquire_next_image();
        assert(outOfDate.status.code == rhi::StatusCode::out_of_date);
        rhi::detail::set_metal_acquire_fault_for_testing(
            rhi::detail::MetalAcquireFault::none);
        assert(swapchain.resize({48, 36}).ok());
        auto resized = swapchain.acquire_next_image();
        assert(resized.ok());
        assert(queue.present(swapchain, resized.imageIndex).ok());

        // Detaching the layer's device represents a lost native surface.
        layer.device = nil;
        const auto lost = swapchain.acquire_next_image();
        assert(lost.status.code == rhi::StatusCode::surface_lost);
        CAMetalLayer* replacementLayer = [CAMetalLayer layer];
        auto replacementSurfaceResult = device.create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                       .handle = (__bridge void*)replacementLayer},
            .initialExtent = {24, 18},
        });
        assert(replacementSurfaceResult.ok());
        auto replacementSurface =
            std::move(replacementSurfaceResult).value();
        auto replacementSwapchainResult = device.create_swapchain(
            replacementSurface, {.extent = {24, 18}});
        assert(replacementSwapchainResult.ok());
        auto replacementSwapchain =
            std::move(replacementSwapchainResult).value();
        auto replacementImage = replacementSwapchain.acquire_next_image();
        assert(replacementImage.ok());
        assert(queue
                   .present(replacementSwapchain,
                            replacementImage.imageIndex)
                   .ok());

        // Hardware device removal is not safe to induce in CI. A private,
        // non-installed native-backend hook proves loss propagation and that
        // recovery requires a newly created device and dependent objects.
        auto lossListResult = pool.allocate();
        assert(lossListResult.ok());
        auto lossList = std::move(lossListResult).value();
        assert(lossList.begin().ok());
        assert(lossList.end().ok());
        std::array<rhi::CommandList*, 1> lossLists{&lossList};
        rhi::detail::set_metal_device_loss_for_testing(true);
        assert(queue.submit(lossLists).code == rhi::StatusCode::device_lost);
        assert(device.lost());
        rhi::detail::set_metal_device_loss_for_testing(false);
        assert(queue.submit(lossLists).code == rhi::StatusCode::device_lost);
        auto recoveredDeviceResult = adapter.request_device({
            .requiredFeatures = {rhi::Feature::presentation},
        });
        assert(recoveredDeviceResult.ok());
        auto recoveredDevice = std::move(recoveredDeviceResult).value();
        assert(!recoveredDevice.lost());
    }
    return 0;
}

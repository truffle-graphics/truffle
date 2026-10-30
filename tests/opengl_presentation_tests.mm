// Presents real frames through the macOS GL backend into a test-owned
// NSWindow/NSView and checks the window back buffer pixels, resizing,
// out-of-date drawables, unsupported modes and main-thread ownership.

#import <AppKit/AppKit.h>

#include "truffle/rhi/opengl_backend.hpp"

#include "gl_graphics_tests.hpp"
#include "opengl_backend_test.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <thread>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

using namespace truffle;

constexpr std::array<std::uint8_t, 4> kBlue{0, 0, 255, 255};
constexpr std::array<std::uint8_t, 4> kGreen{0, 255, 0, 255};
constexpr std::array<std::uint8_t, 4> kRed{255, 0, 0, 255};

void pump_events() {
    for (int index = 0; index < 20; ++index) {
        while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                   untilDate:nil
                                                      inMode:NSDefaultRunLoopMode
                                                     dequeue:YES]) {
            [NSApp sendEvent:event];
        }
        [[NSRunLoop currentRunLoop]
            runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

rhi::Extent2D backing_extent(NSView* view) {
    const NSRect backing = [view convertRectToBacking:view.bounds];
    return {static_cast<std::uint32_t>(NSWidth(backing)),
            static_cast<std::uint32_t>(NSHeight(backing))};
}

std::array<std::uint8_t, 4> captured(const rhi::detail::OpenGLPresentCapture& capture,
                                     std::uint32_t x, std::uint32_t y) {
    const auto offset = (static_cast<std::size_t>(y) * capture.extent.width + x) * 4u;
    return {capture.rgba[offset], capture.rgba[offset + 1],
            capture.rgba[offset + 2], capture.rgba[offset + 3]};
}

struct Frame {
    rhi::Device& device;
    rhi::Queue& queue;
    rhi::CommandPool& pool;
    rhi::Semaphore& rendered;
    std::uint64_t value = 0;

    // Clears the image blue, then draws green into scissor rows [0, h/2):
    // the first texture rows must land at the top of the window.
    rhi::StatusCode present(rhi::Swapchain& swapchain, rhi::Pipeline* pipeline,
                            rhi::StatusCode expectedAcquire,
                            rhi::ClearColor clear = {0.0F, 0.0F, 1.0F, 1.0F}) {
        auto acquired = swapchain.acquire_next_image();
        assert(acquired.ok());
        assert(acquired.status.code == expectedAcquire);
        const auto extent = acquired.image->desc().extent;
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
            .extent = {extent.width, extent.height},
            .colorAttachments = {{.texture = acquired.image, .clear = clear}},
        });
        assert(renderResult.ok());
        auto render = std::move(renderResult).value();
        if (pipeline != nullptr) {
            assert(render.bind_pipeline(*pipeline).ok());
            const std::array viewports{rhi::Viewport{
                0, 0, static_cast<float>(extent.width),
                static_cast<float>(extent.height), 0, 1}};
            const std::array scissors{
                rhi::ScissorRect{0, 0, extent.width, extent.height / 2}};
            assert(render.set_viewports(0, viewports).ok());
            assert(render.set_scissors(0, scissors).ok());
            assert(render.draw(3).ok());
        }
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
        ++value;
        std::array<rhi::CommandList*, 1> lists{&list};
        const std::array<rhi::SemaphoreWait, 1> waits{{
            {.semaphore = acquired.available,
             .value = acquired.availableValue,
             .stages = rhi::PipelineStage::color_attachment_output},
        }};
        const std::array<rhi::SemaphoreSignal, 1> signals{{
            {.semaphore = &rendered, .value = value},
        }};
        assert(queue.submit({.commandLists = lists, .waits = waits,
                             .signals = signals})
                   .ok());
        const std::array<rhi::SemaphoreWait, 1> presentWaits{{
            {.semaphore = &rendered, .value = value,
             .stages = rhi::PipelineStage::bottom},
        }};
        return queue
            .present({.swapchain = &swapchain,
                      .imageIndex = acquired.imageIndex,
                      .waits = presentWaits})
            .code;
    }
};

void expect_split(rhi::Extent2D drawable) {
    const auto capture = rhi::detail::opengl_last_present_capture_for_testing();
    assert(capture.extent.width == drawable.width &&
           capture.extent.height == drawable.height);
    const auto x = drawable.width / 2;
    assert(captured(capture, x, 0) == kGreen);
    assert(captured(capture, x, drawable.height / 4) == kGreen);
    assert(captured(capture, x, drawable.height * 3 / 4) == kBlue);
    assert(captured(capture, x, drawable.height - 1) == kBlue);
}

} // namespace

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];

        std::size_t diagnostics = 0;
        auto instanceResult = rhi::create_opengl_instance({
            .debugCallback = &tests::count_native_diagnostic,
            .debugUserData = &diagnostics,
        });
        assert(instanceResult.ok());
        auto instance = std::move(instanceResult).value();
        auto adapterResult = instance.adapter(0);
        assert(adapterResult.ok());
        auto adapter = std::move(adapterResult).value();
        assert(adapter.info().presentation);
        auto deviceResult = adapter.request_device({
            .requiredFeatures = {rhi::Feature::presentation},
        });
        assert(deviceResult.ok());
        auto device = std::move(deviceResult).value();

        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(80, 80, 96, 64)
                      styleMask:NSWindowStyleMaskTitled
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.releasedWhenClosed = NO;
        NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 96, 64)];
        view.wantsBestResolutionOpenGLSurface = YES;
        window.contentView = view;
        [window orderFrontRegardless];
        pump_events();

        // Only a host NSView is a GL surface; a CAMetalLayer-kind handle or
        // a non-view object is rejected.
        assert(device
                   .create_surface({.native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                                               .handle = (__bridge void*)view},
                                    .initialExtent = {8, 8}})
                   .status()
                   .code == rhi::StatusCode::unsupported);
        NSObject* notView = [[NSObject alloc] init];
        assert(device
                   .create_surface({.native = {.kind = rhi::NativeSurfaceKind::cocoa_view,
                                               .handle = (__bridge void*)notView},
                                    .initialExtent = {8, 8}})
                   .status()
                   .code == rhi::StatusCode::invalid_argument);
        [notView release];

        // Surface creation is AppKit work and stays on the main thread.
        rhi::StatusCode offThread = rhi::StatusCode::ok;
        std::thread{[&] {
            offThread = device
                            .create_surface({.native = {.kind = rhi::NativeSurfaceKind::cocoa_view,
                                                        .handle = (__bridge void*)view},
                                             .initialExtent = {8, 8}})
                            .status()
                            .code;
        }}.join();
        assert(offThread == rhi::StatusCode::invalid_state);

        auto drawable = backing_extent(view);
        auto surfaceResult = device.create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::cocoa_view,
                       .handle = (__bridge void*)view},
            .initialExtent = drawable,
            .debugName = "GL test view",
        });
        assert(surfaceResult.ok());
        auto surface = std::move(surfaceResult).value();
        assert(device
                   .create_swapchain(surface, {.extent = drawable,
                                               .format = rhi::TextureFormat::rgba8_unorm,
                                               .presentMode = rhi::PresentMode::mailbox})
                   .status()
                   .code == rhi::StatusCode::unsupported);
        assert(device
                   .create_swapchain(surface, {.extent = drawable,
                                               .format = rhi::TextureFormat::rgba16_float})
                   .status()
                   .code == rhi::StatusCode::unsupported);

        auto queueResult = device.queue(rhi::QueueKind::graphics);
        auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
        auto renderedResult = device.create_semaphore();
        assert(queueResult.ok() && poolResult.ok() && renderedResult.ok());
        auto queue = std::move(queueResult).value();
        auto pool = std::move(poolResult).value();
        auto rendered = std::move(renderedResult).value();
        Frame frame{device, queue, pool, rendered};

        auto vertex = tests::gl_fixture_shader(device, true, rhi::ShaderStage::vertex, R"(
const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
void main() { gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0); }
)");
        auto fragment = tests::gl_fixture_shader(device, true, rhi::ShaderStage::fragment, R"(
layout(location=0) out vec4 outputColor;
void main() { outputColor = vec4(0.0, 1.0, 0.0, 1.0); }
)");
        auto pipelineResult = device.create_pipeline({
            .vertexShader = &vertex,
            .fragmentShader = &fragment,
            .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}},
            .dynamicState = rhi::DynamicState::viewport | rhi::DynamicState::scissor,
        });
        assert(pipelineResult.ok());
        auto pipeline = std::move(pipelineResult).value();

        rhi::detail::set_opengl_present_capture_for_testing(true);
        {
            auto swapchainResult = device.create_swapchain(
                surface, {.extent = drawable,
                          .format = rhi::TextureFormat::rgba8_unorm,
                          .presentMode = rhi::PresentMode::fifo,
                          .imageCount = 2,
                          .debugName = "GL test swapchain"});
            assert(swapchainResult.ok());
            auto swapchain = std::move(swapchainResult).value();

            for (int index = 0; index < 3; ++index) {
                assert(frame.present(swapchain, &pipeline, rhi::StatusCode::ok) ==
                       rhi::StatusCode::ok);
                expect_split(drawable);
            }

            // Acquire and present are AppKit work too.
            rhi::StatusCode offThreadAcquire = rhi::StatusCode::ok;
            std::thread{[&] {
                offThreadAcquire = swapchain.acquire_next_image().status.code;
            }}.join();
            assert(offThreadAcquire == rhi::StatusCode::invalid_state);

            // The host resizes its window: the old image is still usable but
            // suboptimal, and present scales it into the new drawable.
            [window setContentSize:NSMakeSize(128, 80)];
            pump_events();
            const auto resized = backing_extent(view);
            assert(resized.width != drawable.width &&
                   resized.height != drawable.height);
            assert(frame.present(swapchain, &pipeline, rhi::StatusCode::suboptimal) ==
                   rhi::StatusCode::suboptimal);
            const auto scaled = rhi::detail::opengl_last_present_capture_for_testing();
            assert(scaled.extent.width == resized.width &&
                   scaled.extent.height == resized.height);
            assert(swapchain.resize(resized).ok());
            assert(frame.present(swapchain, &pipeline, rhi::StatusCode::ok) ==
                   rhi::StatusCode::ok);
            expect_split(resized);
            drawable = resized;

            // A view the host removed from its window has no drawable.
            NSView* placeholder = [[NSView alloc] initWithFrame:view.frame];
            window.contentView = placeholder;
            pump_events();
            assert(swapchain.acquire_next_image().status.code ==
                   rhi::StatusCode::out_of_date);
            window.contentView = view;
            pump_events();
            [placeholder release];
            drawable = backing_extent(view);
            assert(swapchain.resize(drawable).ok());
            assert(frame.present(swapchain, &pipeline, rhi::StatusCode::ok) ==
                   rhi::StatusCode::ok);
            expect_split(drawable);
        }

        // A BGRA swapchain presents the same colors.
        {
            auto swapchainResult = device.create_swapchain(
                surface, {.extent = drawable,
                          .format = rhi::TextureFormat::bgra8_unorm,
                          .presentMode = rhi::PresentMode::immediate});
            assert(swapchainResult.ok());
            auto swapchain = std::move(swapchainResult).value();
            assert(frame.present(swapchain, nullptr, rhi::StatusCode::ok,
                                 {1.0F, 0.0F, 0.0F, 1.0F}) == rhi::StatusCode::ok);
            const auto capture = rhi::detail::opengl_last_present_capture_for_testing();
            assert(captured(capture, 0, 0) == kRed);
            assert(captured(capture, drawable.width - 1, drawable.height - 1) == kRed);
        }
        rhi::detail::set_opengl_present_capture_for_testing(false);
        assert(diagnostics == 0);

        [window close];
        [window release];
        [view release];
    }
    return 0;
}

#pragma clang diagnostic pop

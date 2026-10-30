#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "scene.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

namespace {

using namespace truffle;
using minimum_renderer::require;

struct HostWindow {
    NSWindow* window;
    CAMetalLayer* layer;

    explicit HostWindow(bool smoke) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:smoke ? NSApplicationActivationPolicyAccessory
                                        : NSApplicationActivationPolicyRegular];
        if (NSScreen.screens.count == 0) {
            throw std::runtime_error("minimum Renderer needs a logged-in macOS display");
        }
        window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, smoke ? 320 : 960, smoke ? 240 : 640)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        if (window == nil) {
            throw std::runtime_error("minimum Renderer could not create its host window");
        }
        window.releasedWhenClosed = NO;
        window.title = @"Truffle minimum Renderer - Escape to close";
        layer = [CAMetalLayer layer];
        window.contentView.wantsLayer = YES;
        window.contentView.layer = layer;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [NSApp finishLaunching];
    }

    ~HostWindow() { [window close]; }

    void events() {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate distantPast]
                                                  inMode:NSDefaultRunLoopMode dequeue:YES];
            if (event == nil) {
                break;
            }
            if (event.type == NSEventTypeKeyDown && event.keyCode == 53) {
                [window close];
            } else {
                [NSApp sendEvent:event];
            }
        }
        [NSApp updateWindows];
    }

    rhi::Extent2D extent() {
        const auto bounds = [window.contentView convertRectToBacking:window.contentView.bounds];
        if (bounds.size.width < 1 || bounds.size.height < 1 || window.miniaturized) {
            return {0, 0};
        }
        if (bounds.size.width > std::numeric_limits<std::uint32_t>::max() ||
            bounds.size.height > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("minimum Renderer drawable size is out of range");
        }
        layer.contentsScale = window.backingScaleFactor;
        return {static_cast<std::uint32_t>(bounds.size.width),
                static_cast<std::uint32_t>(bounds.size.height)};
    }
};

int run(bool smoke) {
    HostWindow host(smoke);
    auto instance = require(rhi::create_metal_instance());
    auto adapter = require(instance.adapter(0));
    auto device = require(adapter.request_device({.requiredFeatures = {rhi::Feature::presentation}}));
    auto extent = host.extent();
    auto surface = require(device.create_surface({
        .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                   .handle = (__bridge void*)host.layer},
        .initialExtent = extent,
    }));
    auto swapchain = require(device.create_swapchain(surface, {
        .extent = extent, .format = rhi::TextureFormat::bgra8_unorm,
    }));
    renderer::DepthTarget depth(device);
    require(depth.resize(extent));
    minimum_renderer::Scene scene(device, rhi::TextureFormat::bgra8_unorm);
    scene.set_cube();
    const auto start = std::chrono::steady_clock::now();
    std::uint32_t frames = 0;
    std::uint32_t resizeCount = 0;
    bool firstResize = false;
    bool secondResize = false;
    while (host.window.visible && (!smoke || frames < 12)) {
        @autoreleasepool {
            host.events();
            if (!host.window.visible) {
                break;
            }
            if (smoke && std::chrono::steady_clock::now() - start > std::chrono::seconds(45)) {
                throw std::runtime_error("minimum Renderer window smoke timed out");
            }
            if (smoke && frames == 4 && !firstResize) {
                [host.window setContentSize:NSMakeSize(480, 300)];
                firstResize = true;
            }
            if (smoke && frames == 8 && !secondResize) {
                [host.window setContentSize:NSMakeSize(256, 256)];
                secondResize = true;
            }
            const auto next = host.extent();
            if (next.width == 0 || next.height == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            if (next.width != extent.width || next.height != extent.height) {
                require(swapchain.resize(next));
                require(depth.resize(next));
                extent = next;
                ++resizeCount;
                std::cout << "drawable/depth resize " << extent.width << 'x' << extent.height << '\n';
            }
            const float angle = smoke ? static_cast<float>(frames) * 0.1F
                : std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const std::array objects{
                minimum_renderer::Object{
                    .centerScale = {-0.75F, 0, 0, 1},
                    .orientation = {0, std::sin(angle * .5F), 0, std::cos(angle * .5F)},
                    .albedo = {1, .3F, .1F, 1}},
                minimum_renderer::Object{
                    .centerScale = {.75F, 0, -.5F, 1},
                    .orientation = {std::sin(angle * .35F), 0, 0, std::cos(angle * .35F)},
                    .albedo = {.1F, .5F, 1, 1}},
            };
            scene.set_objects(objects);
            minimum_renderer::View view;
            view.viewProjection = renderer::multiply(
                require(renderer::perspective(1.05F,
                    static_cast<float>(extent.width) / static_cast<float>(extent.height), .1F, 100)),
                require(renderer::look_at({0, 1, 4}, {0, 0, 0})));
            view.lightDirectionIntensity = {-1, -2, -3, 1};
            scene.set_view(view);
            renderer::FrameGraph graph;
            graph.add_node(std::make_unique<renderer::RenderPassNode>(
                rhi::RenderPassDesc{
                    .colorAttachments = {{.clear = {.03F, .04F, .07F, 1}}},
                    .depthStencilAttachment = {.texture = depth.texture()},
                }, std::vector{scene.batch()}, true));
            const auto status = scene.render(graph, &swapchain);
            if (status.code == base::StatusCode::out_of_date) {
                std::cerr << "swapchain out of date; recreating at "
                          << extent.width << 'x' << extent.height << '\n';
                require(swapchain.resize(extent));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            require(status);
            if (!scene.stats().presented || scene.stats().renderBatchesExecuted != 1 ||
                depth.texture()->desc().extent.width != extent.width ||
                depth.texture()->desc().extent.height != extent.height) {
                throw std::runtime_error("minimum Renderer did not present a matching-depth frame");
            }
            ++frames;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if (smoke && (frames != 12 || resizeCount != 2)) {
        throw std::runtime_error("window smoke requires 12 presented frames and two actual resizes");
    }
    std::cout << "minimum Renderer: presented=" << frames << " resizes=" << resizeCount << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc != 1 && !smoke) {
        std::cerr << "usage: truffle_minimum_renderer [--smoke]\n";
        return 2;
    }
    @autoreleasepool {
        try {
            return run(smoke);
        } catch (const std::exception& error) {
            std::cerr << "minimum Renderer: " << error.what() << '\n';
            return 1;
        }
    }
}

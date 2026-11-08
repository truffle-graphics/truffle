#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "truffle/gui/dock/dock.hpp"
#include "truffle/gui/specialist/surface.hpp"
#include "truffle/gui/ui.hpp"
#include "truffle/renderer/specialist/frame_stream.hpp"
#include "truffle/renderer/specialist/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace truffle;
template <typename T> T need(base::Result<T> result) {
    if (!result.ok()) { throw std::runtime_error(result.status().message); }
    return std::move(result).value();
}
void need(const base::Status& status) {
    if (!status.ok()) { throw std::runtime_error(status.message); }
}
void expect(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
}

@interface TFLDockView : NSView <NSWindowDelegate> {
@public
    std::function<void(const truffle::gui::InputEvent&)> forward;
    std::function<void()> closing;
}
@end
@implementation TFLDockView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)pointer:(NSEvent*)event action:(truffle::gui::PointerAction)action {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    auto callback = forward;
    if (callback) {
        callback(truffle::gui::PointerEvent{
            .action = action, .position = {static_cast<float>(p.x), static_cast<float>(p.y)},
            .button = truffle::gui::PointerButton::primary});
    }
}
- (void)mouseDown:(NSEvent*)event { [self pointer:event action:truffle::gui::PointerAction::down]; }
- (void)mouseUp:(NSEvent*)event { [self pointer:event action:truffle::gui::PointerAction::up]; }
- (void)mouseDragged:(NSEvent*)event { [self pointer:event action:truffle::gui::PointerAction::move]; }
- (void)mouseMoved:(NSEvent*)event { [self pointer:event action:truffle::gui::PointerAction::move]; }
- (void)mouseExited:(NSEvent*)event { [self pointer:event action:truffle::gui::PointerAction::leave]; }
- (void)keyDown:(NSEvent*)event {
    auto callback = forward;
    if (callback && (event.keyCode == 49 || event.keyCode == 36 || event.keyCode == 48)) {
        callback(truffle::gui::KeyEvent{
            .key = event.keyCode == 48 ? truffle::gui::Key::tab :
                   event.keyCode == 36 ? truffle::gui::Key::enter : truffle::gui::Key::space});
    }
}
- (void)keyUp:(NSEvent*)event {
    auto callback = forward;
    if (callback && (event.keyCode == 49 || event.keyCode == 36)) {
        callback(truffle::gui::KeyEvent{
            .key = event.keyCode == 36 ? truffle::gui::Key::enter : truffle::gui::Key::space,
            .down = false});
    }
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    auto callback = forward;
    if (callback) { callback(truffle::gui::FocusEvent{true}); }
}
- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    auto callback = forward;
    if (callback) { callback(truffle::gui::FocusEvent{false}); }
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    auto callback = closing;
    if (callback) { callback(); }
    return NO;
}
@end

namespace {
struct CocoaServices final : gui::Host {
    void set_cursor(gui::Cursor cursor) override {
        switch (cursor) {
        case gui::Cursor::resize_horizontal: [[NSCursor resizeLeftRightCursor] set]; break;
        case gui::Cursor::resize_vertical: [[NSCursor resizeUpDownCursor] set]; break;
        case gui::Cursor::hand: [[NSCursor pointingHandCursor] set]; break;
        default: [[NSCursor arrowCursor] set]; break;
        }
    }
};

// Host-owned native presentation. Each window has its own drawable, swapchain,
// pacing and streaming buffers; the application Context and live panels are shared.
struct Window {
    gui::Context& context;
    NSWindow* native;
    TFLDockView* view;
    CAMetalLayer* layer;
    CocoaServices services;
    std::unique_ptr<gui::Surface> gui_surface;
    rhi::Surface surface;
    rhi::Swapchain swapchain;
    renderer::specialist::PipelineCache programs;
    renderer::specialist::Renderer renderer;
    renderer::specialist::FramePacer pacer;
    gui::specialist::Painter painter;
    rhi::Extent2D extent{};
    std::size_t frames = 0;
    std::size_t drawn_elements = 0;

    Window(rhi::Device& device, gui::Context& context, bool detached, const gui::Rect& rect)
        : context(context), programs(device), renderer(device, &programs),
          pacer(need(renderer::specialist::FramePacer::create(device, 2))),
          painter(need(gui::specialist::Painter::create(
              device, programs, {.color_format = rhi::TextureFormat::bgra8_unorm}))) {
        native = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(rect.x, rect.y, rect.width, rect.height)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
            backing:NSBackingStoreBuffered defer:NO];
        if (native == nil) { throw std::runtime_error("Cocoa window creation failed"); }
        native.releasedWhenClosed = NO;
        native.acceptsMouseMovedEvents = YES;
        native.title = detached ? @"Live detached panel — close to redock" :
                                  @"Truffle Dock — drag the left tab outside; click panel to change color";
        view = [[TFLDockView alloc] initWithFrame:NSMakeRect(0, 0, rect.width, rect.height)];
        view.wantsLayer = YES;
        layer = [CAMetalLayer layer];
        view.layer = layer;
        native.contentView = view;
        native.delegate = view;
        [native makeFirstResponder:view];
        [view addTrackingArea:[[NSTrackingArea alloc]
            initWithRect:NSZeroRect
            options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                    NSTrackingActiveAlways | NSTrackingInVisibleRect
            owner:view userInfo:nil]];
        if (detached) {
            gui_surface = need(gui::Surface::create(context));
            gui_surface->set_host(&services);
        }
        resize(context);
        surface = need(device.create_surface({
            .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                       .handle = (__bridge void*)layer},
            .initialExtent = extent}));
        swapchain = need(device.create_swapchain(
            surface, {.extent = extent, .format = rhi::TextureFormat::bgra8_unorm}));
        view->forward = [this, &context](const gui::InputEvent& event) {
            const auto status = gui_surface ? gui_surface->handle(event) : context.handle(event);
            if (!status.ok()) { std::cerr << "Input refused: " << status.message << '\n'; }
        };
    }
    ~Window() {
        if (&context.host() == &services) { context.set_host(nullptr); }
        (void)pacer.wait_idle();
        native.delegate = nil;
        view->forward = {};
        view->closing = {};
        [native orderOut:nil];
        [native close];
    }
    void resize(gui::Context& context) {
        const NSRect logical = view.bounds;
        const NSRect pixels = [view convertRectToBacking:logical];
        extent = {static_cast<std::uint32_t>(std::max(1.0, pixels.size.width)),
                  static_cast<std::uint32_t>(std::max(1.0, pixels.size.height))};
        const float scale = static_cast<float>(native.backingScaleFactor);
        layer.contentsScale = native.backingScaleFactor;
        const gui::Size size{static_cast<float>(logical.size.width),
                             static_cast<float>(logical.size.height)};
        need(gui_surface ? gui_surface->set_viewport(size, scale) : context.set_viewport(size, scale));
        if (swapchain.valid() && swapchain.desc().extent != extent) {
            need(pacer.wait_idle());
            need(swapchain.resize(extent));
        }
    }
    void draw(gui::Context& context) {
        const auto slot = need(pacer.begin_frame());
        auto batches = need(gui_surface ? gui::specialist::SurfaceInterop::record(*gui_surface, painter, slot, extent)
                                         : painter.record(context, slot, extent));
        drawn_elements = painter.last_stats().elements;
        renderer::specialist::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
            rhi::RenderPassDesc{.colorAttachments = {{.clear = {0.035F, 0.035F, 0.045F, 1}}}},
            std::move(batches), true));
        auto status = renderer.render(graph, &swapchain, pacer.signal());
        pacer.end_frame(renderer.last_frame_stats().submitted);
        if (status.code == base::StatusCode::out_of_date) {
            need(swapchain.resize(extent));
            context.request_frame();
        } else { need(status); ++frames; }
    }
};

class Windows final : public gui::dock::DockWindowHost {
public:
    rhi::Device& device;
    gui::Context& context;
    gui::dock::DockSpace dock;
    Window* primary = nullptr;
    std::map<std::string, std::unique_ptr<Window>, std::less<>> windows;
    // Cocoa may still be executing the closing delegate/input callback.
    std::vector<std::unique_ptr<Window>> retired;
    std::size_t closed = 0;
    Windows(rhi::Device& d, gui::Context& c) : device(d), context(c) {}
    ~Windows() override { if (dock.alive()) { (void)dock.shutdown(); } }
    base::Result<gui::Surface*> open_window(
        gui::Context& c, const gui::dock::DockWindowRequest& request) override {
        if (&c != &context || windows.contains(request.panel)) {
            return base::Status::failure(base::StatusCode::invalid_argument, "Invalid native request");
        }
        try {
            if (primary == nullptr) {
                return base::Status::failure(base::StatusCode::invalid_state, "No primary Cocoa window");
            }
            const NSPoint in_window = [primary->view
                convertPoint:NSMakePoint(request.rect.x, request.rect.y) toView:nil];
            const NSPoint screen = [primary->native convertPointToScreen:in_window];
            const gui::Rect native_rect{
                static_cast<float>(screen.x), static_cast<float>(screen.y) - request.rect.height,
                request.rect.width, request.rect.height};
            auto window = std::make_unique<Window>(device, context, true, native_rect);
            auto* surface = window->gui_surface.get();
            window->view->closing = [this, id = request.panel] {
                const auto status = dock.redock(id);
                if (!status.ok()) { std::cerr << "Close refused: " << status.message << '\n'; }
            };
            windows.emplace(request.panel, std::move(window));
            return surface;
        } catch (const std::exception& error) {
            return base::Status::failure(base::StatusCode::backend_error, error.what());
        }
    }
    void show_window(std::string_view panel) noexcept override {
        auto& window = *windows.find(panel)->second;
        [window.native makeKeyAndOrderFront:nil];
    }
    void close_window(std::string_view panel) noexcept override {
        const auto found = windows.find(panel);
        if (found == windows.end()) { return; }
        auto window = std::move(found->second);
        windows.erase(found);
        (void)window->pacer.wait_idle();
        (void)window->gui_surface->release_input();
        window->view->forward = {};
        window->view->closing = {};
        window->native.delegate = nil;
        [window->native orderOut:nil];
        [window->native close];
        retired.push_back(std::move(window));
        ++closed;
    }
};

void post_pointer(Window& window, NSEventType type, gui::Point point) {
    const NSPoint local = NSMakePoint(point.x, point.y);
    const NSPoint location = [window.view convertPoint:local toView:nil];
    NSEvent* event = [NSEvent mouseEventWithType:type location:location modifierFlags:0
        timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.native.windowNumber
        context:nil eventNumber:1 clickCount:1 pressure:type == NSEventTypeLeftMouseUp ? 0 : 1];
    [NSApp postEvent:event atStart:NO];
}

int run(bool smoke) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:smoke ? NSApplicationActivationPolicyAccessory
                                    : NSApplicationActivationPolicyRegular];
    if (NSScreen.screens.count == 0) { throw std::runtime_error("A logged-in Cocoa display is required"); }
    auto instance = need(rhi::create_metal_instance());
    auto adapter = need(instance.adapter(0));
    auto device = need(adapter.request_device({.requiredFeatures = {rhi::Feature::presentation}}));
    gui::Context context;
    Windows host(device, context);
    host.dock = need(gui::dock::DockSpace::create(context, context.root()));
    auto& dock = host.dock;
    need(dock.set_window_host(&host));
    Window main(device, context, false, {80, 120, 640, 420});
    host.primary = &main;
    context.set_host(&main.services);
    int clicks = 0;
    gui::Ui ui{context};
    auto live = dock.panel(ui, "live", "Live panel");
    const auto panel = live.button("").weight(1).on_click([&] { ++clicks; });
    need(context.set_semantics(panel.element(), {
        .role = gui::Context::Role::button, .label = "Live panel — click to change color"}));
    dock.panel(ui, "other", "Other panel").on_paint([](gui::Painter& paint) {
        paint.rect({0, 0, paint.size().width, paint.size().height},
                   gui::Color{0.08F, 0.15F, 0.23F, 1});
    }, gui::PaintLayer::background);
    need(dock.set_layout(gui::dock::DockNode::split(gui::dock::Axis::horizontal,
        {gui::dock::DockNode::tabs({"live"}), gui::dock::DockNode::tabs({"other"})})));
    bool running = true;
    main.view->closing = [&] { need(dock.shutdown()); running = false; };
    [main.native makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    const auto iterate = [&] {
        @autoreleasepool {
            NSDate* until = [NSDate dateWithTimeIntervalSinceNow:0.005];
            for (int count = 0; count < 128; ++count) {
                NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until
                    inMode:NSDefaultRunLoopMode dequeue:YES];
                if (event == nil) { break; }
                until = [NSDate distantPast];
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
            host.retired.clear();
            main.resize(context);
            need(context.set_placement(dock.element(), gui::Rect{
                0, 0, static_cast<float>(main.view.bounds.size.width),
                static_cast<float>(main.view.bounds.size.height)}));
            for (auto& [id, window] : host.windows) { (void)id; window->resize(context); }
            if (context.needs_frame(gui::Context::Clock::now())) {
                need(context.prepare_frame(gui::Context::Clock::now()).status);
                // The live widget and its handler remain the same object in either view.
                main.draw(context);
                for (auto& [id, window] : host.windows) { (void)id; window->draw(context); }
            }
        }
    };
    const auto settle = [&] {
        for (int i = 0; i < 12; ++i) { iterate(); }
    };
    settle();
    if (!smoke) {
        int last_clicks = clicks;
        while (running) {
            iterate();
            if (clicks != last_clicks) {
                last_clicks = clicks;
                need(context.set_decoration(panel.element(),
                    {.fill = clicks % 2 ? gui::Color{0.12F, 0.55F, 0.3F, 1}
                                       : gui::Color{0.4F, 0.22F, 0.55F, 1}}));
            }
        }
    } else {
        const auto saved = dock.save();
        const auto same_content = panel.element();
        // These are actual Cocoa events, delivered through NSApp and the native view.
        post_pointer(main, NSEventTypeLeftMouseDown, {30, 12});
        post_pointer(main, NSEventTypeLeftMouseDragged, {700, 80});
        post_pointer(main, NSEventTypeLeftMouseUp, {700, 80});
        settle();
        expect(host.windows.size() == 1 && dock.detached("live"), "Native outside drop did not detach");
        Window& detached = *host.windows.at("live");
        expect(main.native.visible && detached.native.visible &&
               main.native.windowNumber != detached.native.windowNumber,
               "Expected two distinct visible OS windows");
        expect(detached.frames > 0 && detached.drawn_elements > 0, "Detached content was not rendered");
        [detached.native setContentSize:NSMakeSize(380, 260)];
        settle();
        expect(need(context.bounds(same_content)).width == 380, "Detached resize did not reach live GUI");
        [detached.native makeKeyAndOrderFront:nil];
        settle();
        post_pointer(detached, NSEventTypeLeftMouseDown, {100, 100});
        post_pointer(detached, NSEventTypeLeftMouseUp, {100, 100});
        settle();
        expect(clicks == 1, "Detached Cocoa input did not activate original live widget");
        need(context.set_decoration(panel.element(), {.fill = {0.15F, 0.7F, 0.25F, 1}}));
        settle();
        expect(panel.element() == same_content && dock.save() == saved, "Live identity/save was not preserved");
        const auto detached_frames = detached.frames;
        [detached.native performClose:nil];
        settle();
        expect(host.windows.empty() && host.closed == 1 && !dock.detached("live"),
               "Native close did not redock/clean up");
        expect(need(context.parent(live.element())) == dock.element() &&
               need(context.parent(same_content)) == live.element() && dock.save() == saved,
               "Redock lost original panel location/state");
        [main.native makeKeyAndOrderFront:nil];
        settle();
        post_pointer(main, NSEventTypeLeftMouseDown, {80, 100});
        post_pointer(main, NSEventTypeLeftMouseUp, {80, 100});
        settle();
        expect(clicks == 2, "Redocked live widget lost its handler/state");
        need(dock.detach("live", {450, 160, 320, 220}));
        settle();
        need(dock.restore(saved));
        settle();
        expect(host.windows.empty() && host.closed == 2, "Restore did not close detached resources");
        std::cout << "native dock smoke: two OS windows/swapchains; detached frames="
                  << detached_frames << "; resize=380x260; live clicks=" << clicks
                  << "; redock+restore closed=" << host.closed << '\n';
    }
    need(dock.shutdown());
    context.set_host(nullptr);
    need(main.pacer.wait_idle());
    return 0;
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        try { return run(argc == 2 && std::string_view(argv[1]) == "--smoke"); }
        catch (const std::exception& error) {
            std::cerr << "GUI dock window: " << error.what() << '\n';
            return 1;
        }
    }
}

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "truffle/gui/context.hpp"
#include "truffle/gui/painter.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

// A host-owned macOS window that draws a Truffle GUI through the Renderer and
// redraws only when the GUI needs a frame. The host owns the window, the event
// loop, input translation and presentation; the GUI owns the element tree.

namespace {

using namespace truffle;
using Clock = gui::Context::Clock;

template <typename T>
T require(base::Result<T> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

void require(const base::Status& status) {
    if (!status.ok()) {
        throw std::runtime_error(status.message);
    }
}

gui::Modifiers modifiers_of(NSEventModifierFlags flags) {
    gui::Modifiers modifiers = gui::Modifiers::none;
    if ((flags & NSEventModifierFlagShift) != 0) {
        modifiers = modifiers | gui::Modifiers::shift;
    }
    if ((flags & NSEventModifierFlagControl) != 0) {
        modifiers = modifiers | gui::Modifiers::control;
    }
    if ((flags & NSEventModifierFlagOption) != 0) {
        modifiers = modifiers | gui::Modifiers::alt;
    }
    if ((flags & NSEventModifierFlagCommand) != 0) {
        modifiers = modifiers | gui::Modifiers::super;
    }
    return modifiers;
}

gui::Key key_of(unsigned short code) {
    switch (code) {
    case 0: return gui::Key::a;
    case 8: return gui::Key::c;
    case 9: return gui::Key::v;
    case 36: return gui::Key::enter;
    case 48: return gui::Key::tab;
    case 49: return gui::Key::space;
    case 51: return gui::Key::backspace;
    case 53: return gui::Key::escape;
    case 54: case 55: return gui::Key::super;
    case 56: case 60: return gui::Key::shift;
    case 58: case 61: return gui::Key::alt;
    case 59: case 62: return gui::Key::control;
    case 115: return gui::Key::home;
    case 116: return gui::Key::page_up;
    case 117: return gui::Key::delete_forward;
    case 119: return gui::Key::end;
    case 121: return gui::Key::page_down;
    case 123: return gui::Key::left;
    case 124: return gui::Key::right;
    case 125: return gui::Key::down;
    case 126: return gui::Key::up;
    default: return gui::Key::unknown; // the native code still travels with the event
    }
}

gui::PointerButton button_of(NSEvent* event) {
    switch (event.buttonNumber) {
    case 0: return gui::PointerButton::primary;
    case 1: return gui::PointerButton::secondary;
    case 2: return gui::PointerButton::middle;
    case 3: return gui::PointerButton::back;
    case 4: return gui::PointerButton::forward;
    default: return gui::PointerButton::none;
    }
}

// Cocoa implementation of the GUI's platform services. Input-method placement
// needs an NSTextInputClient view, which arrives with text editing.
class CocoaHost final : public gui::Host {
public:
    void set_cursor(gui::Cursor cursor) override {
        if (cursor == gui::Cursor::hidden) {
            if (!hidden_) {
                [NSCursor hide];
                hidden_ = true;
            }
            return;
        }
        if (hidden_) {
            [NSCursor unhide];
            hidden_ = false;
        }
        switch (cursor) {
        case gui::Cursor::text: [[NSCursor IBeamCursor] set]; break;
        case gui::Cursor::hand: [[NSCursor pointingHandCursor] set]; break;
        case gui::Cursor::crosshair: [[NSCursor crosshairCursor] set]; break;
        case gui::Cursor::move: [[NSCursor openHandCursor] set]; break;
        case gui::Cursor::resize_horizontal: [[NSCursor resizeLeftRightCursor] set]; break;
        case gui::Cursor::resize_vertical: [[NSCursor resizeUpDownCursor] set]; break;
        case gui::Cursor::not_allowed: [[NSCursor operationNotAllowedCursor] set]; break;
        default: [[NSCursor arrowCursor] set]; break;
        }
    }

    std::string clipboard_text() override {
        NSString* text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
        return text == nil ? std::string{} : std::string(text.UTF8String);
    }

    void set_clipboard_text(std::string_view text) override {
        NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                   length:text.size()
                                                 encoding:NSUTF8StringEncoding];
        [NSPasteboard.generalPasteboard clearContents];
        [NSPasteboard.generalPasteboard setString:value forType:NSPasteboardTypeString];
    }

private:
    bool hidden_ = false;
};

gui::Decoration look(gui::Color fill, float radius = 0, gui::Color border = {},
                     float border_width = 0) {
    gui::Decoration decoration;
    decoration.fill = fill;
    decoration.corner_radius = radius;
    decoration.border = border;
    decoration.border_width = border_width;
    return decoration;
}

// The application: a sidebar with a blinking indicator, and a canvas where the
// primary button places a marker. Only these changes cause frames.
struct App {
    gui::Context context;
    gui::ElementId canvas;
    gui::ElementId marker;
    gui::ElementId indicator;
    bool indicator_on = true;

    App() {
        const gui::ElementId root = context.root();
        require(context.set_decoration(root, look({0.08F, 0.09F, 0.12F, 1})));
        require(context.set_layout(root, std::make_shared<gui::StackLayout>(
            gui::StackOptions{.axis = gui::Axis::horizontal, .spacing = 12})));
        gui::LayoutParams root_params;
        root_params.padding = {12, 12, 12, 12};
        require(context.set_layout_params(root, root_params));

        const gui::ElementId sidebar = require(context.create(root));
        gui::LayoutParams sidebar_params;
        sidebar_params.width = 180;
        sidebar_params.padding = {12, 12, 12, 12};
        require(context.set_layout_params(sidebar, sidebar_params));
        require(context.set_layout(sidebar, std::make_shared<gui::StackLayout>(
            gui::StackOptions{.spacing = 8})));
        require(context.set_decoration(
            sidebar, look({0.14F, 0.15F, 0.19F, 1}, 10, {0.3F, 0.32F, 0.4F, 1}, 1)));
        for (int item = 0; item < 4; ++item) {
            const gui::ElementId row = require(context.create(sidebar));
            gui::LayoutParams row_params;
            row_params.height = 28;
            require(context.set_layout_params(row, row_params));
            require(context.set_decoration(
                row, look(item == 0 ? gui::Color{0.25F, 0.45F, 0.9F, 1}
                                    : gui::Color{0.2F, 0.21F, 0.26F, 1}, 6)));
        }
        indicator = require(context.create(sidebar));
        gui::LayoutParams indicator_params;
        indicator_params.width = 16;
        indicator_params.height = 16;
        require(context.set_layout_params(indicator, indicator_params));
        require(context.set_decoration(indicator, look({0.3F, 0.9F, 0.5F, 1}, 8)));

        canvas = require(context.create(root));
        gui::LayoutParams canvas_params;
        canvas_params.weight = 1;
        require(context.set_layout_params(canvas, canvas_params));
        require(context.set_decoration(
            canvas, look({0.11F, 0.12F, 0.16F, 1}, 10, {0.3F, 0.32F, 0.4F, 1}, 1)));
        const gui::Color card_colors[] = {
            {0.9F, 0.4F, 0.3F, 1}, {0.95F, 0.75F, 0.25F, 1}, {0.35F, 0.7F, 0.95F, 0.6F}};
        for (int card = 0; card < 3; ++card) {
            const gui::ElementId element = require(context.create(canvas));
            gui::LayoutParams card_params;
            card_params.width = 160;
            card_params.height = 100;
            card_params.position = {24.0F + 60.0F * static_cast<float>(card),
                                    24.0F + 50.0F * static_cast<float>(card)};
            require(context.set_layout_params(element, card_params));
            require(context.set_decoration(
                element, look(card_colors[card], 12, {1, 1, 1, 0.8F}, 2)));
        }
        marker = require(context.create(canvas));
        gui::LayoutParams marker_params;
        marker_params.width = 20;
        marker_params.height = 20;
        marker_params.position = {300, 200};
        require(context.set_layout_params(marker, marker_params));
        require(context.set_decoration(marker, look({1, 1, 1, 1}, 10, {0, 0, 0, 1}, 2)));
    }

    // Application reactions until the GUI routes events to elements itself.
    void handle(const gui::InputEvent& event) {
        context.handle(event);
        const gui::InputState& input = context.input();
        if (const auto* pointer = std::get_if<gui::PointerEvent>(&event)) {
            const auto found = context.absolute_bounds(canvas);
            const gui::Rect bounds = found.ok() ? found.value() : gui::Rect{};
            const bool over = input.pointer && input.pointer->x >= bounds.x &&
                              input.pointer->y >= bounds.y &&
                              input.pointer->x < bounds.x + bounds.width &&
                              input.pointer->y < bounds.y + bounds.height;
            context.host().set_cursor(over ? gui::Cursor::crosshair : gui::Cursor::arrow);
            if (over && input.pressed(gui::PointerButton::primary) &&
                pointer->action != gui::PointerAction::up) {
                gui::LayoutParams params = require(context.layout_params(marker));
                params.position = {input.pointer->x - bounds.x - 10,
                                   input.pointer->y - bounds.y - 10};
                require(context.set_layout_params(marker, params));
            }
        } else if (const auto* key = std::get_if<gui::KeyEvent>(&event)) {
            if (key->down && key->key == gui::Key::c &&
                gui::has(key->modifiers, gui::Modifiers::super)) {
                const gui::Point at = require(context.layout_params(marker)).position;
                context.host().set_clipboard_text(
                    "marker " + std::to_string(at.x) + ", " + std::to_string(at.y));
            }
        }
    }

    // Timed update: the indicator blinks.
    void tick(Clock::time_point now, std::chrono::milliseconds period, bool repeat) {
        const auto due = context.next_update();
        if (due && *due <= now) {
            indicator_on = !indicator_on;
            require(context.set_decoration(
                indicator, look({0.3F, 0.9F, 0.5F, indicator_on ? 1.0F : 0.2F}, 8)));
            if (repeat) {
                context.request_frame_at(now + period);
            }
        }
    }
};

struct Window {
    NSWindow* window;
    CAMetalLayer* layer;
    bool focused = false;

    explicit Window(bool smoke) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:smoke ? NSApplicationActivationPolicyAccessory
                                        : NSApplicationActivationPolicyRegular];
        if (NSScreen.screens.count == 0) {
            throw std::runtime_error("the GUI window needs a logged-in macOS display");
        }
        window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 720, 480)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        if (window == nil) {
            throw std::runtime_error("could not create the GUI window");
        }
        window.releasedWhenClosed = NO;
        window.acceptsMouseMovedEvents = YES;
        window.title = @"Truffle GUI - click the canvas, Escape to close";
        layer = [CAMetalLayer layer];
        window.contentView.wantsLayer = YES;
        window.contentView.layer = layer;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [NSApp finishLaunching];
    }

    ~Window() { [window close]; }

    gui::Point position(NSEvent* event) const {
        const NSPoint point = [window.contentView convertPoint:event.locationInWindow
                                                      fromView:nil];
        return {static_cast<float>(point.x),
                static_cast<float>(window.contentView.bounds.size.height - point.y)};
    }

    // Waits for events until `until`, then drains the queue into `forward`.
    void pump(NSDate* until, const std::function<void(const gui::InputEvent&)>& forward) {
        NSDate* wait = until;
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:wait
                                                   inMode:NSDefaultRunLoopMode dequeue:YES];
            wait = [NSDate distantPast];
            if (event == nil) {
                break;
            }
            if (event.type == NSEventTypeKeyDown && event.keyCode == 53) {
                [window close];
                return;
            }
            if (event.window == window) {
                translate(event, forward);
            }
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
        if (window.keyWindow != focused) {
            focused = window.keyWindow;
            forward(gui::FocusEvent{.focused = focused});
        }
    }

    void translate(NSEvent* event, const std::function<void(const gui::InputEvent&)>& forward) {
        const gui::Modifiers modifiers = modifiers_of(event.modifierFlags);
        const auto pointer = [&](gui::PointerAction action, gui::PointerButton button) {
            forward(gui::PointerEvent{.action = action, .position = position(event),
                                      .button = button, .modifiers = modifiers});
        };
        switch (event.type) {
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            pointer(gui::PointerAction::move, gui::PointerButton::none);
            break;
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
            pointer(gui::PointerAction::down, button_of(event));
            break;
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
            pointer(gui::PointerAction::up, button_of(event));
            break;
        case NSEventTypeMouseExited:
            pointer(gui::PointerAction::leave, gui::PointerButton::none);
            break;
        case NSEventTypeScrollWheel:
            forward(gui::WheelEvent{.position = position(event),
                                    .delta_x = static_cast<float>(event.scrollingDeltaX),
                                    .delta_y = static_cast<float>(event.scrollingDeltaY),
                                    .precise = event.hasPreciseScrollingDeltas == YES,
                                    .modifiers = modifiers});
            break;
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp: {
            const bool down = event.type == NSEventTypeKeyDown;
            forward(gui::KeyEvent{.key = key_of(event.keyCode), .down = down,
                                  .repeat = down && event.ARepeat == YES,
                                  .modifiers = modifiers, .native = event.keyCode});
            NSString* characters = event.characters;
            if (down && characters.length > 0 &&
                !gui::has(modifiers, gui::Modifiers::super) &&
                !gui::has(modifiers, gui::Modifiers::control)) {
                const unichar first = [characters characterAtIndex:0];
                // Skips control characters and AppKit's function-key range.
                if (first >= 0x20 && first != 0x7F && (first < 0xF700 || first > 0xF8FF)) {
                    forward(gui::TextEvent{characters.UTF8String});
                }
            }
            break;
        }
        case NSEventTypeFlagsChanged:
            forward(gui::KeyEvent{.key = key_of(event.keyCode),
                                  .down = modifiers != gui::Modifiers::none,
                                  .modifiers = modifiers, .native = event.keyCode});
            break;
        default:
            break;
        }
    }

    // Logical size, content scale and drawable pixels; false while minimized.
    bool measure(gui::Size& logical, float& scale, rhi::Extent2D& pixels) {
        const NSRect bounds = window.contentView.bounds;
        const NSRect backing = [window.contentView convertRectToBacking:bounds];
        if (backing.size.width < 1 || backing.size.height < 1 || window.miniaturized) {
            return false;
        }
        if (backing.size.width > std::numeric_limits<std::uint32_t>::max() ||
            backing.size.height > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("drawable size is out of range");
        }
        layer.contentsScale = window.backingScaleFactor;
        logical = {static_cast<float>(bounds.size.width), static_cast<float>(bounds.size.height)};
        scale = static_cast<float>(window.backingScaleFactor);
        pixels = {static_cast<std::uint32_t>(backing.size.width),
                  static_cast<std::uint32_t>(backing.size.height)};
        return true;
    }
};

int run(bool smoke) {
    Window host_window(smoke);
    CocoaHost host;
    App app;
    app.context.set_host(&host);

    auto instance = require(rhi::create_metal_instance());
    auto adapter = require(instance.adapter(0));
    auto device = require(adapter.request_device(
        {.requiredFeatures = {rhi::Feature::presentation}}));
    gui::Size logical;
    float scale = 1;
    rhi::Extent2D extent{};
    if (!host_window.measure(logical, scale, extent)) {
        throw std::runtime_error("the GUI window has no drawable area");
    }
    auto surface = require(device.create_surface({
        .native = {.kind = rhi::NativeSurfaceKind::cocoa_layer,
                   .handle = (__bridge void*)host_window.layer},
        .initialExtent = extent,
    }));
    auto swapchain = require(device.create_swapchain(
        surface, {.extent = extent, .format = rhi::TextureFormat::bgra8_unorm}));
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    auto pacer = require(renderer::FramePacer::create(device, 2));
    auto painter = require(gui::Painter::create(
        device, programs, {.color_format = rhi::TextureFormat::bgra8_unorm}));

    const auto blink = std::chrono::milliseconds(smoke ? 40 : 500);

    std::uint32_t frames = 0;
    const auto draw = [&]() {
        const auto now = Clock::now();
        app.tick(now, blink, !smoke);
        static_cast<void>(app.context.prepare_frame(now));
        const auto current = swapchain.desc().extent;
        if (current.width != extent.width || current.height != extent.height) {
            require(swapchain.resize(extent));
        }
        const auto slot = require(pacer.begin_frame());
        auto batches = require(painter.record(app.context, slot, extent));
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{.colorAttachments = {{.clear = {0, 0, 0, 1}}}},
            std::move(batches), true));
        const auto status = renderer.render(graph, &swapchain, pacer.signal());
        pacer.end_frame(renderer.last_frame_stats().submitted);
        if (status.code == base::StatusCode::out_of_date) {
            require(swapchain.resize(extent));
            app.context.request_frame();
            return;
        }
        require(status);
        ++frames;
    };

    // One host iteration: sleep until input or the next timed update (capped
    // in smoke mode so the script advances), sync the viewport, draw if needed.
    const auto iterate = [&](std::chrono::milliseconds cap) {
        @autoreleasepool {
            const auto now = Clock::now();
            NSDate* until = [NSDate distantFuture];
            if (app.context.needs_frame(now)) {
                until = [NSDate distantPast];
            } else {
                double seconds = std::numeric_limits<double>::infinity();
                if (const auto due = app.context.next_update()) {
                    seconds = std::chrono::duration<double>(*due - now).count();
                }
                if (cap.count() > 0) {
                    seconds = std::min(seconds, std::chrono::duration<double>(cap).count());
                }
                if (std::isfinite(seconds)) {
                    until = [NSDate dateWithTimeIntervalSinceNow:std::max(seconds, 0.0)];
                }
            }
            host_window.pump(until, [&](const gui::InputEvent& event) { app.handle(event); });
            if (!host_window.window.visible) {
                return;
            }
            if (!host_window.measure(logical, scale, extent)) {
                return; // minimized: nothing to draw until it returns
            }
            require(app.context.set_viewport(logical, scale));
            if (app.context.needs_frame(Clock::now())) {
                draw();
            }
        }
    };

    if (!smoke) {
        app.context.request_frame_at(Clock::now() + blink);
        while (host_window.window.visible) {
            iterate(std::chrono::milliseconds(0));
        }
        require(pacer.wait_idle());
        std::cout << "Truffle GUI window: frames=" << frames << '\n';
        return 0;
    }

    // Smoke script: count frames to prove that only changes are drawn.
    const auto settle = [&](int iterations) {
        for (int i = 0; i < iterations && host_window.window.visible; ++i) {
            iterate(std::chrono::milliseconds(5));
        }
    };
    const auto expect = [&](std::uint32_t wanted, const char* what) {
        if (frames != wanted) {
            throw std::runtime_error(std::string(what) + ": drew " + std::to_string(frames) +
                                     " frames, expected " + std::to_string(wanted));
        }
        std::cout << what << ": " << frames << " frame(s)\n";
    };
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (frames == 0 && Clock::now() < deadline) {
        iterate(std::chrono::milliseconds(5));
    }
    expect(1, "initial frame");
    // Scheduled after the first frame, which may outlast the short smoke period.
    app.context.request_frame_at(Clock::now() + blink);
    while (frames == 1 && Clock::now() < deadline) {
        iterate(std::chrono::milliseconds(5));
    }
    settle(10);
    expect(2, "one timed update, then idle");

    const auto canvas = require(app.context.absolute_bounds(app.canvas));
    const gui::Point inside{canvas.x + 50, canvas.y + 60};
    app.handle(gui::PointerEvent{.action = gui::PointerAction::move, .position = inside});
    settle(4);
    expect(2, "pointer movement alone");
    app.handle(gui::PointerEvent{.action = gui::PointerAction::down, .position = inside,
                                 .button = gui::PointerButton::primary});
    app.handle(gui::PointerEvent{.action = gui::PointerAction::up, .position = inside,
                                 .button = gui::PointerButton::primary});
    settle(4);
    expect(3, "marker placed");

    [host_window.window setContentSize:NSMakeSize(560, 400)];
    settle(4);
    expect(4, "window resize");
    if (painter.last_stats().elements != 12) {
        throw std::runtime_error("expected twelve decorated elements to be drawn");
    }
    require(pacer.wait_idle());
    std::cout << "Truffle GUI window smoke: frames=" << frames << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc != 1 && !smoke) {
        std::cerr << "usage: truffle_gui_window [--smoke]\n";
        return 2;
    }
    @autoreleasepool {
        try {
            return run(smoke);
        } catch (const std::exception& error) {
            std::cerr << "Truffle GUI window: " << error.what() << '\n';
            return 1;
        }
    }
}

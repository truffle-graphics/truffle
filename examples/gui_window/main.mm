#include "truffle/gui/specialist/text.hpp"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreText/CoreText.h>

#include "demo.hpp"
#include "input_view.hpp"

#include "truffle/gui/context.hpp"
#include "truffle/gui/specialist/painter.hpp"
#include "truffle/renderer/specialist/frame_stream.hpp"
#include "truffle/renderer/specialist/renderer.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    case 6: return gui::Key::z;
    case 7: return gui::Key::x;
    case 16: return gui::Key::y;
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

class CocoaHost final : public gui::Host {
public:
    explicit CocoaHost(TFLInputView* view) : view_(view) {}
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

    void set_text_input_area(std::optional<gui::Rect> area) override {
        view_->candidateArea = area;
        [view_.inputContext invalidateCharacterCoordinates];
    }
    void set_text_input_state(std::optional<gui::TextInputState> state) override {
        const bool ended = view_->textState &&
            (!state || (view_->textState->marked && !state->marked));
        view_->textState = std::move(state);
        if (ended) { [view_.inputContext discardMarkedText]; }
    }

private:
    TFLInputView* view_;
    bool hidden_ = false;
};

gui::FontId load_host_face(gui::TextEngine& engine, CTFontRef font) {
    CFTypeRef attribute = CTFontCopyAttribute(font, kCTFontURLAttribute);
    if (attribute == nullptr || CFGetTypeID(attribute) != CFURLGetTypeID()) {
        if (attribute != nullptr) { CFRelease(attribute); }
        throw std::runtime_error("the host system font has no loadable font URL");
    }
    NSURL* url = CFBridgingRelease(attribute);
    NSError* error = nil;
    NSData* bytes = [NSData dataWithContentsOfURL:url options:0 error:&error];
    if (bytes == nil) {
        throw std::runtime_error("could not read the host font: " +
            std::string(error.localizedDescription.UTF8String));
    }
    return require(engine.load_font(
        {static_cast<const std::byte*>(bytes.bytes), bytes.length}, 14));
}

gui::FontId load_host_font(gui::TextEngine& engine) {
    CTFontRef system = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 14, nullptr);
    if (system == nullptr) { throw std::runtime_error("could not resolve the host system font"); }
    // File discovery is a host policy. The portable engine receives only
    // explicitly loaded font bytes and an ordered chain.
    const auto primary = load_host_face(engine, system);
    std::vector<gui::FontId> fallbacks;
    for (NSString* sample in @[@"\u05E9\u05DC\u05D5\u05DD", @"\u0645\u0631\u062D\u0628\u0627"]) {
        CTFontRef face = CTFontCreateForString(system, (__bridge CFStringRef)sample,
                                              CFRangeMake(0, sample.length));
        if (face == nullptr) {
            CFRelease(system);
            throw std::runtime_error("could not resolve a host fallback font");
        }
        fallbacks.push_back(load_host_face(engine, face));
        CFRelease(face);
    }
    CFRelease(system);
    require(engine.set_fallbacks(primary, fallbacks));
    const auto proof = require(engine.shape(
        primary, "Latin \u05E9\u05DC\u05D5\u05DD \u0645\u0631\u062D\u0628\u0627 123"));
    if (std::ranges::any_of(proof.glyphs, [](const gui::TextGlyph& glyph) {
            return glyph.id == 0;
        })) {
        throw std::runtime_error("host font chain did not cover the multilingual demo");
    }
    return primary;
}

struct Window {
    NSWindow* window;
    CAMetalLayer* layer;
    TFLInputView* view;
    bool focused = false;

    explicit Window(bool smoke) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:smoke ? NSApplicationActivationPolicyAccessory
                                        : NSApplicationActivationPolicyRegular];
        if (NSScreen.screens.count == 0) {
            throw std::runtime_error("the GUI window needs a logged-in macOS display");
        }
        window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 900, 640)
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        if (window == nil) {
            throw std::runtime_error("could not create the GUI window");
        }
        window.releasedWhenClosed = NO;
        window.acceptsMouseMovedEvents = YES;
        window.title = @"Truffle GUI - widgets and text editing";
        view = [[TFLInputView alloc] initWithFrame:window.contentView.bounds];
        window.contentView = view;
        layer = [CAMetalLayer layer];
        window.contentView.wantsLayer = YES;
        window.contentView.layer = layer;
        [window center];
        [window makeKeyAndOrderFront:nil];
        [window makeFirstResponder:view];
        [NSApp finishLaunching];
    }

    ~Window() {
        view->forward = {};
        view->queryArea = {};
        view->textState.reset();
        [window close];
    }

    gui::Point position(NSEvent* event) const {
        const NSPoint point = [window.contentView convertPoint:event.locationInWindow
                                                      fromView:nil];
        return {static_cast<float>(point.x), static_cast<float>(point.y)};
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
            if (event.type == NSEventTypeKeyDown && event.keyCode == 53 && !view->textState) {
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
            if (!down || ![view hasMarkedText] ||
                gui::has(modifiers, gui::Modifiers::super)) {
                forward(gui::KeyEvent{.key = key_of(event.keyCode), .down = down,
                                  .repeat = down && event.ARepeat == YES,
                                  .modifiers = modifiers, .native = event.keyCode});
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
    CocoaHost host(host_window.view);
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
    renderer::specialist::PipelineCache programs(device);
    renderer::specialist::Renderer renderer(device, &programs);
    auto pacer = require(renderer::specialist::FramePacer::create(device, 2));
    auto painter = require(gui::specialist::Painter::create(
        device, programs, {.color_format = rhi::TextureFormat::bgra8_unorm}));
    auto text = require(gui::specialist::TextInterop::create(device));
    const auto font = load_host_font(text);
    const auto atlas = require(painter.add_texture(require(truffle::gui::specialist::TextInterop::atlas(text)).get()));
    const std::array<std::byte, 16> texels{
        std::byte{255}, std::byte{130}, std::byte{60}, std::byte{255},
        std::byte{70}, std::byte{180}, std::byte{255}, std::byte{255},
        std::byte{70}, std::byte{180}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{130}, std::byte{60}, std::byte{255}};
    auto image = require(renderer::specialist::SampledTexture::upload(device, {
        .extent = {2, 2}, .bytes = texels}));
    const auto image_id = require(painter.add_texture(image));
    examples::GuiDemo app(text, font, atlas, image_id);
    app.context.set_host(&host);
    host_window.view->forward = [&](const gui::InputEvent& event) {
        if (!smoke) { app.handle(event); }
    };
    host_window.view->queryArea = [&]() -> std::optional<gui::Rect> {
        if (app.context.focused() != app.editor.element()) { return std::nullopt; }
        return require(require(app.editor.editor()).caret_rect());
    };

    std::uint32_t frames = 0;
    gui::FrameStats last_work;
    const auto report = [](std::string_view what, const gui::FrameStats& work) {
        std::cout << what << ": styles=" << work.styles.resolved
                  << " measured=" << work.layout.measured
                  << " arranged=" << work.layout.arranged
                  << " shapes=" << work.shapes.regenerated << '\n';
    };
    const auto draw = [&]() {
        const auto now = Clock::now();
        last_work = app.context.prepare_frame(now);
        require(last_work.status);
        const auto current = swapchain.desc().extent;
        if (current.width != extent.width || current.height != extent.height) {
            require(swapchain.resize(extent));
        }
        const auto slot = require(pacer.begin_frame());
        auto batches = require(painter.record(app.context, slot, extent));
        renderer::specialist::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
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
        report("frame " + std::to_string(frames), last_work);
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
            host_window.pump(until, [&](const gui::InputEvent& event) {
                if (!smoke || std::holds_alternative<gui::FocusEvent>(event)) {
                    app.handle(event);
                }
            });
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
        while (host_window.window.visible) {
            iterate(std::chrono::milliseconds(0));
        }
        require(pacer.wait_idle());
        std::cout << "Truffle GUI window: frames=" << frames << '\n';
        return 0;
    }

    // Native persistence evidence against the same application/tree as normal use.
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
    settle(10);
    expect(1, "idle host");
    const auto idle = app.context.prepare_frame(Clock::now());
    require(idle.status);
    report("idle work", idle);
    if (idle.styles.resolved != 0 || idle.layout.measured != 0 ||
        idle.layout.arranged != 0 || idle.shapes.regenerated != 0) {
        throw std::runtime_error("idle tree recomputed work");
    }

    require(app.context.set_theme_value("gui.accent", gui::Color{0.95F, 0.5F, 0.25F, 1}));
    settle(4);
    expect(2, "two-element accent change");
    if (last_work.styles.resolved != 2 || last_work.layout.measured != 0 ||
        last_work.layout.arranged != 0 || last_work.shapes.regenerated != 2) {
        throw std::runtime_error("accent change did not affect exactly its two paint dependents");
    }

    require(app.scroll.set_scroll({0, 44}));
    settle(4);
    expect(3, "scroll stored elements");
    if (last_work.styles.resolved != 0 || last_work.layout.measured != 0 ||
        last_work.layout.arranged != 0 || last_work.shapes.regenerated != 0) {
        throw std::runtime_error("scrolling rebuilt stored elements");
    }

    require(app.status.set_text("Button presses: 100"));
    settle(4);
    expect(4, "one label changed");
    if (last_work.styles.resolved != 0 || last_work.layout.measured != 4 ||
        last_work.shapes.regenerated != 1) {
        throw std::runtime_error("text change escaped its four-element measurement path");
    }
    const auto click = [&](gui::ElementId id) {
        const auto bounds = require(app.context.absolute_bounds(id));
        const gui::Point point{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
        app.handle(gui::PointerEvent{.action = gui::PointerAction::down, .position = point,
                                     .button = gui::PointerButton::primary});
        app.handle(gui::PointerEvent{.action = gui::PointerAction::up, .position = point,
                                     .button = gui::PointerButton::primary});
        settle(4);
    };
    click(app.button.element());
    expect(5, "button callback");
    if (app.presses != 1 || app.context.focused() != app.button.element()) {
        throw std::runtime_error("button activation/focus failed");
    }
    click(app.checkbox.element());
    expect(6, "checkbox callback");
    if (require(app.checkbox.checked())) { throw std::runtime_error("checkbox did not toggle"); }
    require(app.context.focus(app.slider.element()));
    app.handle(gui::KeyEvent{.key = gui::Key::left});
    settle(4);
    expect(7, "slider keyboard callback");
    if (require(app.slider.value()) >= 1) { throw std::runtime_error("slider did not change"); }

    [host_window.window setContentSize:NSMakeSize(740, 560)];
    settle(4);
    expect(8, "window resize");
    settle(10);
    expect(8, "idle after changes");

    // Exercise the actual NSTextInputClient bridge, not just GUI TextEvents.
    host_window.view->forward = [&](const gui::InputEvent& event) { app.handle(event); };
    auto editor = require(app.editor.editor());
    require(app.context.handle(gui::FocusEvent{true}));
    require(app.context.focus(editor.element()));
    require(app.editor.set_text("AB"));
    require(editor.select({1, 2}));
    [host_window.view setMarkedText:@"A" selectedRange:NSMakeRange(1, 0)
                  replacementRange:NSMakeRange(NSNotFound, 0)];
    if (![host_window.view hasMarkedText] || require(app.editor.text()) != "AB") {
        throw std::runtime_error("native preedit changed the committed document");
    }
    NSRange actual;
    const auto candidate = [host_window.view firstRectForCharacterRange:NSMakeRange(1, 0)
                                                           actualRange:&actual];
    if (candidate.size.height <= 0) {
        throw std::runtime_error("native input-method candidate area was not published");
    }
    [host_window.view insertText:@"BA" replacementRange:NSMakeRange(NSNotFound, 0)];
    if ([host_window.view hasMarkedText] || require(app.editor.text()) != "ABA") {
        throw std::runtime_error("native input-method commit failed");
    }
    require(editor.undo());
    if (require(app.editor.text()) != "AB") {
        throw std::runtime_error("native input-method commit was not one undo step");
    }
    require(app.context.focus({}));
    host_window.view->forward = {};
    draw();
    const auto edited_idle = app.context.prepare_frame(Clock::now());
    require(edited_idle.status);
    if (edited_idle.layout.measured != 0 || edited_idle.shapes.regenerated != 0) {
        throw std::runtime_error("editable control rebuilt while idle");
    }
    std::cout << "native text editing: preedit, candidate placement, commit and undo passed\n";
    require(pacer.wait_idle());
    std::cout << "Truffle GUI widget demo: elements=" << app.context.element_count()
              << " frames=" << frames << " rows=" << app.rows.size() << '\n';
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

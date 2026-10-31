#include "test_support.hpp"
#include "truffle/gui/context.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace {

using truffle::gui::Color;
using truffle::gui::CompositionEvent;
using truffle::gui::Context;
using truffle::gui::Cursor;
using truffle::gui::Decoration;
using truffle::gui::ElementId;
using truffle::gui::FocusEvent;
using truffle::gui::Key;
using truffle::gui::KeyEvent;
using truffle::gui::LayoutParams;
using truffle::gui::Modifiers;
using truffle::gui::PointerAction;
using truffle::gui::PointerButton;
using truffle::gui::PointerEvent;
using truffle::gui::Size;
using truffle::gui::TextEvent;
using truffle::gui::WheelEvent;
using Clock = Context::Clock;
using std::chrono::milliseconds;

Decoration filled(Color color) {
    Decoration decoration;
    decoration.fill = color;
    return decoration;
}

int frame_scheduling() {
    Context context;
    const auto t0 = Clock::now();
    TRUFFLE_CHECK(context.needs_frame(t0)); // a new tree has never been drawn
    TRUFFLE_CHECK(context.set_viewport({100, 80}).ok());
    const auto first = context.prepare_frame(t0);
    TRUFFLE_CHECK(first.layout.measured == 1 && first.shapes.regenerated == 1);
    TRUFFLE_CHECK(!context.needs_frame(t0));
    TRUFFLE_CHECK(!context.has_dirty());

    // Idle: nothing to draw and no work when a frame is prepared anyway.
    TRUFFLE_CHECK(!context.needs_frame(t0 + milliseconds(500)));
    const auto idle = context.prepare_frame(t0);
    TRUFFLE_CHECK(idle.layout.measured == 0 && idle.layout.arranged == 0 &&
                  idle.shapes.regenerated == 0);

    // Every kind of visible change needs exactly one frame.
    const ElementId panel = context.create(context.root()).value();
    TRUFFLE_CHECK(context.needs_frame(t0));
    static_cast<void>(context.prepare_frame(t0));
    TRUFFLE_CHECK(!context.needs_frame(t0));

    TRUFFLE_CHECK(context.set_decoration(panel, filled({1, 0, 0, 1})).ok());
    TRUFFLE_CHECK(context.needs_frame(t0));
    TRUFFLE_CHECK(context.prepare_frame(t0).shapes.regenerated == 1);
    TRUFFLE_CHECK(!context.needs_frame(t0));
    TRUFFLE_CHECK(context.set_decoration(panel, filled({1, 0, 0, 1})).ok());
    TRUFFLE_CHECK(!context.needs_frame(t0));

    TRUFFLE_CHECK(context.set_clips_children(panel, false).ok());
    TRUFFLE_CHECK(context.needs_frame(t0));
    static_cast<void>(context.prepare_frame(t0));
    TRUFFLE_CHECK(context.set_clips_children(panel, false).ok());
    TRUFFLE_CHECK(!context.needs_frame(t0));

    LayoutParams moved;
    moved.width = 10;
    moved.height = 10;
    moved.position = {5, 5};
    TRUFFLE_CHECK(context.set_layout_params(panel, moved).ok());
    TRUFFLE_CHECK(context.needs_frame(t0));
    static_cast<void>(context.prepare_frame(t0));
    TRUFFLE_CHECK(!context.needs_frame(t0));

    context.request_frame();
    TRUFFLE_CHECK(context.needs_frame(t0));
    static_cast<void>(context.prepare_frame(t0));
    TRUFFLE_CHECK(!context.needs_frame(t0));

    // The viewport resizes the root; the scale changes pixels, not shapes.
    TRUFFLE_CHECK(context.set_viewport({120, 90}).ok());
    TRUFFLE_CHECK(context.needs_frame(t0));
    TRUFFLE_CHECK(context.prepare_frame(t0).shapes.regenerated == 1);
    TRUFFLE_CHECK(context.set_viewport({120, 90}, 2).ok());
    TRUFFLE_CHECK(context.needs_frame(t0) && context.scale() == 2.0F);
    const auto rescaled = context.prepare_frame(t0);
    TRUFFLE_CHECK(rescaled.layout.measured == 0 && rescaled.shapes.regenerated == 0);
    TRUFFLE_CHECK(context.set_viewport({120, 90}, 2).ok());
    TRUFFLE_CHECK(!context.needs_frame(t0));
    TRUFFLE_CHECK((context.viewport() == Size{120, 90}));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    TRUFFLE_CHECK(!context.set_viewport({-1, 10}).ok());
    TRUFFLE_CHECK(!context.set_viewport({10, nan}).ok());
    TRUFFLE_CHECK(!context.set_viewport({10, 10}, 0).ok());
    TRUFFLE_CHECK(!context.set_viewport({10, 10}, nan).ok());
    TRUFFLE_CHECK(!context.needs_frame(t0) && context.scale() == 2.0F);

    // Timed updates: the earliest is reported; due ones are consumed.
    TRUFFLE_CHECK(!context.next_update().has_value());
    context.request_frame_at(t0 + milliseconds(10));
    context.request_frame_at(t0 + milliseconds(5));
    TRUFFLE_CHECK(context.next_update() == t0 + milliseconds(5));
    TRUFFLE_CHECK(!context.needs_frame(t0 + milliseconds(4)));
    TRUFFLE_CHECK(context.needs_frame(t0 + milliseconds(5)));
    static_cast<void>(context.prepare_frame(t0 + milliseconds(5)));
    TRUFFLE_CHECK(context.next_update() == t0 + milliseconds(10));
    TRUFFLE_CHECK(!context.needs_frame(t0 + milliseconds(6)));
    static_cast<void>(context.prepare_frame(t0 + milliseconds(20)));
    TRUFFLE_CHECK(!context.next_update().has_value());
    TRUFFLE_CHECK(!context.needs_frame(t0 + milliseconds(30)));
    return 0;
}

int input_state() {
    Context context;
    static_cast<void>(context.prepare_frame(Clock::now()));
    const auto now = Clock::now();
    const auto& input = context.input();
    TRUFFLE_CHECK(!input.pointer.has_value() && input.focused);

    context.handle(PointerEvent{.action = PointerAction::move, .position = {3, 4}});
    TRUFFLE_CHECK(input.pointer.has_value() && input.pointer->x == 3 && input.pointer->y == 4);
    context.handle(PointerEvent{.action = PointerAction::down,
                                .position = {5, 6},
                                .button = PointerButton::primary,
                                .modifiers = Modifiers::shift});
    TRUFFLE_CHECK(input.pressed(PointerButton::primary));
    TRUFFLE_CHECK(!input.pressed(PointerButton::secondary));
    TRUFFLE_CHECK(truffle::gui::has(input.modifiers, Modifiers::shift));
    context.handle(PointerEvent{.action = PointerAction::down,
                                .position = {5, 6},
                                .button = PointerButton::secondary});
    context.handle(PointerEvent{.action = PointerAction::up,
                                .position = {7, 6},
                                .button = PointerButton::primary});
    TRUFFLE_CHECK(!input.pressed(PointerButton::primary));
    TRUFFLE_CHECK(input.pressed(PointerButton::secondary));
    TRUFFLE_CHECK(input.pointer->x == 7 && input.modifiers == Modifiers::none);
    context.handle(PointerEvent{.action = PointerAction::cancel});
    TRUFFLE_CHECK(input.buttons == 0 && !input.pointer.has_value());

    context.handle(WheelEvent{.position = {8, 9}, .delta_y = -3, .precise = true});
    TRUFFLE_CHECK(input.pointer->x == 8);
    context.handle(PointerEvent{.action = PointerAction::leave});
    TRUFFLE_CHECK(!input.pointer.has_value());

    context.handle(KeyEvent{.key = Key::a,
                            .modifiers = Modifiers::control | Modifiers::super});
    TRUFFLE_CHECK(truffle::gui::has(input.modifiers, Modifiers::super) &&
                  !truffle::gui::has(input.modifiers, Modifiers::alt));
    context.handle(TextEvent{"\xC3\xA9"});

    context.handle(CompositionEvent{.preedit = "abc", .selection_begin = 2,
                                    .selection_end = 9});
    TRUFFLE_CHECK(input.composition.preedit == "abc");
    TRUFFLE_CHECK(input.composition.selection_begin == 2 &&
                  input.composition.selection_end == 3);
    context.handle(CompositionEvent{.preedit = "x", .selection_begin = 4,
                                    .selection_end = 0});
    TRUFFLE_CHECK(input.composition.selection_begin == 1 &&
                  input.composition.selection_end == 1);

    context.handle(PointerEvent{.action = PointerAction::down,
                                .button = PointerButton::middle});
    context.handle(FocusEvent{.focused = false});
    TRUFFLE_CHECK(!input.focused && input.buttons == 0 &&
                  input.modifiers == Modifiers::none && input.composition.preedit.empty());
    context.handle(FocusEvent{.focused = true});
    TRUFFLE_CHECK(input.focused);

    // Input alone changes nothing visible, so it needs no frame.
    TRUFFLE_CHECK(!context.needs_frame(now));
    return 0;
}

class RecordingHost final : public truffle::gui::Host {
public:
    void set_cursor(Cursor next) override { cursor = next; }
    std::string clipboard_text() override { return clipboard; }
    void set_clipboard_text(std::string_view text) override { clipboard = text; }
    void set_text_input_area(std::optional<truffle::gui::Rect> next) override {
        area = next;
    }

    Cursor cursor = Cursor::arrow;
    std::string clipboard;
    std::optional<truffle::gui::Rect> area;
};

int host_services() {
    Context context;
    // Without a host every service is a harmless no-op.
    context.host().set_cursor(Cursor::hand);
    context.host().set_clipboard_text("lost");
    context.host().set_text_input_area(truffle::gui::Rect{1, 2, 3, 4});
    TRUFFLE_CHECK(context.host().clipboard_text().empty());

    RecordingHost recording;
    context.set_host(&recording);
    context.host().set_cursor(Cursor::text);
    context.host().set_clipboard_text("copied");
    context.host().set_text_input_area(truffle::gui::Rect{1, 2, 3, 4});
    TRUFFLE_CHECK(recording.cursor == Cursor::text);
    TRUFFLE_CHECK(context.host().clipboard_text() == "copied");
    TRUFFLE_CHECK(recording.area.has_value() && recording.area->width == 3);

    context.set_host(nullptr);
    TRUFFLE_CHECK(context.host().clipboard_text().empty());
    return 0;
}

} // namespace

int main() {
    if (frame_scheduling() != 0) {
        return 1;
    }
    if (input_state() != 0) {
        return 1;
    }
    return host_services();
}

#include "test_support.hpp"
#include "truffle/gui/context.hpp"

#include <vector>

namespace {
using namespace truffle::gui;

ElementId box(Context& context, ElementId parent, Point position, Size size) {
    const auto id = context.create(parent).value();
    LayoutParams params;
    params.position = position;
    params.width = size.width;
    params.height = size.height;
    const auto status = context.set_layout_params(id, params);
    if (!status.ok()) { return {}; }
    return id;
}

int hit_and_route() {
    Context c;
    const auto panel = box(c, c.root(), {10, 10}, {40, 40});
    const auto first = box(c, panel, {5, 5}, {30, 30});
    const auto last = box(c, panel, {20, 20}, {30, 30});
    static_cast<void>(c.update_layout({100, 100}));
    TFL_CHECK(c.hit_test({35, 35}) == last);
    TFL_CHECK(c.hit_test({20, 20}) == first);
    TFL_CHECK(c.hit_test({55, 35}) == c.root());
    TFL_CHECK(c.set_clips_children(panel, false).ok());
    TFL_CHECK(c.hit_test({55, 35}) == last);
    TFL_CHECK(c.set_interaction(last, {.hit_test = false}).ok());
    TFL_CHECK(c.hit_test({35, 35}) == first);
    TFL_CHECK(c.set_interaction(panel, {.enabled = false}).ok());
    TFL_CHECK(c.hit_test({20, 20}) == c.root());
    TFL_CHECK(c.set_interaction(panel, {}).ok());
    TFL_CHECK(!c.hit_test({100, 20}).valid());

    std::vector<ElementId> visited;
    std::size_t traces = 0;
    c.set_event_trace([&](const RoutedEvent&) { ++traces; });
    const auto record = [&](Context&, RoutedEvent& e) {
        if (e.input<PointerEvent>()) { visited.push_back(e.current); }
    };
    TFL_CHECK(c.set_event_handler(first, record).ok());
    TFL_CHECK(c.set_event_handler(panel, record).ok());
    TFL_CHECK(c.set_event_handler(c.root(), record).ok());
    c.handle(PointerEvent{.position = {20, 20}});
    TFL_CHECK((visited == std::vector<ElementId>{first, panel, c.root()}));
    TFL_CHECK(traces > 0 && c.hovered() == first);
    visited.clear();
    c.set_event_trace({});
    TFL_CHECK(c.set_event_handler(panel, [&](Context&, RoutedEvent& e) {
        if (e.input<PointerEvent>()) {
            visited.push_back(e.current);
            e.stop_propagation();
        }
    }).ok());
    c.handle(PointerEvent{.position = {20, 20}});
    TFL_CHECK((visited == std::vector<ElementId>{first, panel}));

    // Snapshot routing survives growth, handler replacement and target removal.
    TFL_CHECK(c.set_event_handler(first, [&](Context& context, RoutedEvent& e) {
        if (!e.input<PointerEvent>()) { return; }
        visited.push_back(e.current);
        static_cast<void>(context.create(context.root()));
        static_cast<void>(context.destroy(first));
    }).ok());
    visited.clear();
    c.handle(PointerEvent{.position = {20, 20}});
    TFL_CHECK(!c.alive(first) && !c.hovered().valid());
    TFL_CHECK((visited == std::vector<ElementId>{first, panel}));
    return 0;
}

int focus_and_capture() {
    Context c;
    const auto a = box(c, c.root(), {}, {30, 30});
    const auto b = box(c, c.root(), {40, 0}, {30, 30});
    TFL_CHECK(c.set_interaction(a, {.focusable = true}).ok());
    TFL_CHECK(c.set_interaction(b, {.focusable = true}).ok());
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    static_cast<void>(c.prepare_frame(Context::Clock::now()));
    std::vector<Key> keys;
    std::vector<std::uint32_t> routed_pointers;
    int lost = 0;
    int enters = 0;
    int leaves = 0;
    TFL_CHECK(c.set_event_handler(a, [&](Context&, RoutedEvent& e) {
        if (const auto* key = e.input<KeyEvent>()) { keys.push_back(key->key); }
        if (const auto* pointer = e.input<PointerEvent>()) {
            routed_pointers.push_back(pointer->pointer);
        }
        if (std::get_if<CaptureLostEvent>(&e.payload)) { ++lost; }
        if (const auto* hover = std::get_if<HoverEvent>(&e.payload)) {
            hover->entered ? ++enters : ++leaves;
        }
    }).ok());
    c.handle(KeyEvent{.key = Key::tab});
    TFL_CHECK(c.focused() == a);
    c.handle(KeyEvent{.key = Key::a});
    TFL_CHECK(keys.back() == Key::a);
    c.handle(KeyEvent{.key = Key::tab});
    TFL_CHECK(c.focused() == b);
    c.handle(KeyEvent{.key = Key::tab});
    TFL_CHECK(c.focused() == a);
    c.handle(KeyEvent{.key = Key::tab, .modifiers = Modifiers::shift});
    TFL_CHECK(c.focused() == b);
    TFL_CHECK(c.set_interaction(b, {.enabled = false, .focusable = true}).ok());
    TFL_CHECK(!c.focused().valid());
    c.focus_next();
    TFL_CHECK(c.focused() == a);
    TFL_CHECK(!c.focus(b).ok());
    c.handle(PointerEvent{.action = PointerAction::down, .position = {10, 10},
                          .button = PointerButton::primary, .pointer = 7});
    TFL_CHECK(enters == 1 && c.focused() == a);
    TFL_CHECK(c.capture_pointer(a, 7).ok());
    c.handle(PointerEvent{.position = {90, 90}, .pointer = 7});
    TFL_CHECK(routed_pointers.back() == 7 && leaves == 1);
    TFL_CHECK(c.captured(7) == a);
    c.handle(PointerEvent{.action = PointerAction::up, .position = {90, 90},
                          .button = PointerButton::primary, .pointer = 7});
    TFL_CHECK(!c.captured(7).valid() && lost == 1);
    TFL_CHECK(c.capture_pointer(a, 1).ok() && c.capture_pointer(a, 2).ok());
    c.handle(PointerEvent{.action = PointerAction::cancel, .pointer = 1});
    TFL_CHECK(!c.captured(1).valid() && c.captured(2) == a);
    c.handle(FocusEvent{false});
    TFL_CHECK(!c.focused().valid() && !c.captured(2).valid() && lost == 3);
    c.handle(KeyEvent{.key = Key::tab});
    TFL_CHECK(!c.focused().valid());
    c.handle(FocusEvent{true});
    TFL_CHECK(c.focus(a).ok() && c.capture_pointer(a).ok());
    TFL_CHECK(c.destroy(a).ok());
    TFL_CHECK(!c.focused().valid() && !c.captured().valid());
    TFL_CHECK(!c.capture_pointer(a).ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    return 0;
}
} // namespace

int main() {
    if (hit_and_route() != 0) { return 1; }
    return focus_and_capture();
}

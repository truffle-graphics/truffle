#include "truffle/gui/specialist/text.hpp"
#include "gui_text_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/widgets.hpp"

#include <limits>

namespace {
using namespace truffle;
using namespace truffle::gui;

void place(Context& c, ElementId id, Point position, Size size) {
    LayoutParams params;
    params.position = position;
    params.width = size.width;
    params.height = size.height;
    tests::require(c.set_layout_params(id, params));
}

int interactions() {
    Context c;
    int clicks = 0;
    int toggles = 0;
    int values = 0;
    auto button = tests::require(Widget::create(c, c.root(), {
        .kind = WidgetKind::button, .activated = [&] { ++clicks; }}));
    auto check = tests::require(Widget::create(c, c.root(), {
        .kind = WidgetKind::checkbox, .toggled = [&](bool) { ++toggles; }}));
    auto slider = tests::require(Widget::create(c, c.root(), {
        .kind = WidgetKind::slider, .step = 0.1F, .value_changed = [&](float) { ++values; }}));
    place(c, button.element(), {}, {100, 32});
    place(c, check.element(), {0, 40}, {100, 32});
    place(c, slider.element(), {0, 80}, {120, 24});
    TFL_CHECK(c.set_viewport({200, 160}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    const auto click = [&](Point p) {
        tests::require(c.handle(PointerEvent{.action = PointerAction::down, .position = p,
                       .button = PointerButton::primary}));
        tests::require(c.handle(PointerEvent{.action = PointerAction::up, .position = p,
                       .button = PointerButton::primary}));
    };
    click({20, 16});
    TFL_CHECK(clicks == 1 && c.focused() == button.element());
    TFL_CHECK(!c.captured().valid());
    TFL_CHECK(c.handle(KeyEvent{.key = Key::space}).ok());
    TFL_CHECK(clicks == 1);
    TFL_CHECK(c.handle(KeyEvent{.key = Key::space, .down = false}).ok());
    TFL_CHECK(clicks == 2);
    TFL_CHECK(c.handle(KeyEvent{.key = Key::enter}).ok());
    TFL_CHECK(c.handle(KeyEvent{.key = Key::enter, .repeat = true}).ok());
    TFL_CHECK(clicks == 3);
    TFL_CHECK(c.handle(PointerEvent{.action = PointerAction::down, .position = {20, 16},
              .button = PointerButton::primary}).ok());
    TFL_CHECK(c.handle(PointerEvent{.action = PointerAction::up, .position = {190, 150},
              .button = PointerButton::primary}).ok());
    TFL_CHECK(clicks == 3);
    TFL_CHECK(button.set_enabled(false).ok());
    click({20, 16});
    TFL_CHECK(clicks == 3 && !c.focused().valid());
    TFL_CHECK(button.set_enabled(true).ok());
    click({20, 56});
    TFL_CHECK(check.checked().value() && toggles == 1);
    TFL_CHECK(check.set_checked(false).ok());
    TFL_CHECK(!check.checked().value() && toggles == 1);
    TFL_CHECK(c.handle(KeyEvent{.key = Key::space}).ok());
    TFL_CHECK(c.handle(FocusEvent{false}).ok());
    TFL_CHECK(c.handle(FocusEvent{true}).ok());
    TFL_CHECK(c.focus(check.element()).ok());
    TFL_CHECK(c.handle(KeyEvent{.key = Key::space, .down = false}).ok());
    TFL_CHECK(toggles == 1);
    TFL_CHECK(c.focus(slider.element()).ok());
    TFL_CHECK(c.handle(KeyEvent{.key = Key::right}).ok());
    TFL_CHECK(std::abs(slider.value().value() - 0.1F) < 0.001F && values == 1);
    TFL_CHECK(c.handle(KeyEvent{.key = Key::end}).ok());
    TFL_CHECK(slider.value().value() == 1 && values == 2);
    TFL_CHECK(slider.set_value(0).ok() && values == 2);
    TFL_CHECK(c.handle(PointerEvent{.action = PointerAction::down, .position = {60, 90},
              .button = PointerButton::primary, .pointer = 4}).ok());
    TFL_CHECK(std::abs(slider.value().value() - 0.5F) < 0.001F);
    TFL_CHECK(c.handle(PointerEvent{.position = {300, 90}, .pointer = 4}).ok());
    TFL_CHECK(slider.value().value() == 1);
    TFL_CHECK(c.handle(PointerEvent{.action = PointerAction::cancel, .pointer = 4}).ok());
    TFL_CHECK(!c.captured(4).valid());
    TFL_CHECK(!slider.set_value(std::numeric_limits<float>::quiet_NaN()).ok());
    TFL_CHECK(!button.set_checked(true).ok());
    TFL_CHECK(!Widget::create(c, c.root(), {.kind = WidgetKind::slider, .max = 0}).ok());
    TFL_CHECK(c.destroy(button.element()).ok());
    TFL_CHECK(!button.alive() && !button.set_enabled(true).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    const auto idle = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(idle.status.ok() && idle.styles.resolved == 0 &&
              idle.layout.measured == 0 && idle.layout.arranged == 0 &&
              idle.shapes.regenerated == 0);
    return 0;
}

int scroll_and_passive_widgets() {
    Context c;
    const auto panel = tests::require(Widget::create(c, c.root()));
    place(c, panel.element(), {}, {180, 160});
    auto scroll = tests::require(Widget::create(c, panel.element(),
                                            {.kind = WidgetKind::scroll_area}));
    place(c, scroll.element(), {}, {100, 80});
    const auto row = c.create(scroll.content()).value();
    place(c, row, {}, {120, 200});
    TFL_CHECK(c.set_viewport({200, 180}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(scroll.set_scroll({1000, 1000}).ok());
    TFL_CHECK((scroll.scroll().value() == Point{36, 136}));
    const auto moved = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(moved.status.ok() && moved.styles.resolved == 0 &&
              moved.layout.measured == 0 && moved.layout.arranged == 0 &&
              moved.shapes.regenerated == 0);
    TFL_CHECK(scroll.set_scroll({0, 0}).ok());
    TFL_CHECK(c.handle(WheelEvent{.position = {30, 30}, .delta_y = -1}).ok());
    TFL_CHECK(scroll.scroll().value().y == 20);
    TFL_CHECK(c.focus(scroll.element()).ok());
    TFL_CHECK(c.handle(KeyEvent{.key = Key::down}).ok());
    TFL_CHECK(scroll.scroll().value().y == 40);
    TFL_CHECK(c.handle(KeyEvent{.key = Key::home}).ok());
    TFL_CHECK(scroll.scroll().value().y == 0);
    TFL_CHECK(scroll.set_scroll({1000, 1000}).ok());
    place(c, row, {}, {20, 20});
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(scroll.scroll().value() == Point{});
    TFL_CHECK(c.hit_test({150, 40}) == panel.element());
    const auto label = tests::require(Widget::create(c, panel.element(),
                                               {.kind = WidgetKind::label}));
    auto image = tests::require(Widget::create(c, panel.element(),
                         {.kind = WidgetKind::image, .image = {1}, .image_size = {20, 20}}));
    place(c, label.element(), {110, 20}, {20, 20});
    place(c, image.element(), {110, 20}, {20, 20});
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.hit_test({125, 35}) == panel.element());
    TFL_CHECK(c.semantics(label.element()).value().role == Context::Role::label);
    TFL_CHECK(c.semantics(image.element()).value().role == Context::Role::image);
    TFL_CHECK(image.set_text("Preview").ok());
    TFL_CHECK(c.semantics(image.element()).value().label == "Preview");
    TFL_CHECK(!Widget::create(c, c.root(), {.kind = WidgetKind::image}).ok());
    return 0;
}

int text_widgets() {
#if TFL_TEST_GUI_TEXT
    auto backend = tests::make_null_context();
    auto engine = tests::require(specialist::TextInterop::create(backend.device, {{64, 64}}));
    const auto font = tests::require(engine.load_font(tests::gui_test_font(), 7));
    Context c;
    auto label = tests::require(Widget::create(c, c.root(), {
        .kind = WidgetKind::label, .text = "A", .text_engine = &engine,
        .font = font, .atlas = {1}}));
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(label.set_text("B").ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.semantics(label.element()).value().label == "B");
    TFL_CHECK(label.set_text("B").ok() && !c.has_dirty());
    TFL_CHECK(c.set_property_override(label.element(), Property::text_color,
                                     Color{1, 0, 0, 1}).ok());
    const auto changed = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(changed.status.ok() && changed.styles.resolved == 1 &&
              changed.layout.measured == 0 && changed.shapes.regenerated == 1);
#endif
    return 0;
}
} // namespace

int main() {
    if (interactions() != 0 || scroll_and_passive_widgets() != 0) { return 1; }
    return text_widgets();
}

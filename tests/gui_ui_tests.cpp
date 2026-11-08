#include "test_support.hpp"

#if TFL_TEST_GUI_TEXT
#include "truffle/gui/specialist/text.hpp"
#include "gui_text_fixture.hpp"
#include "menu_example.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/gui/dock/dock.hpp"
#include "truffle/gui/ui.hpp"

#include <string>
#include <vector>

namespace {
using namespace truffle;
using namespace truffle::gui;

TextStyle text_style;

void frame(Context& c) { static_cast<void>(c.prepare_frame(Context::Clock::now())); }

Point center(Context& c, ElementId id) {
    const auto rect = c.absolute_bounds(id).value();
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

void click(Context& c, Point p) {
    static_cast<void>(c.handle(PointerEvent{.action = PointerAction::down, .position = p,
                                            .button = PointerButton::primary}));
    static_cast<void>(c.handle(PointerEvent{.action = PointerAction::up, .position = p,
                                            .button = PointerButton::primary}));
}

float spacing(const Element& element) {
    const auto value = element.property(Property::spacing);
    return value && std::holds_alternative<float>(*value) ? std::get<float>(*value) : -1.0F;
}

int fluent_callbacks() {
    Context c;
    Ui ui{c, text_style};
    TFL_CHECK(c.set_viewport({300, 300}).ok());
    int clicks = 0;
    int presses = 0;
    int releases = 0;
    std::vector<bool> hovers;
    bool toggled = false;
    float value = -1;
    auto root = ui.root();
    auto row = root.row();
    auto button = row.button("Go").width(100).on_click([&] { ++clicks; })
        .on_hover([&](bool inside) { hovers.push_back(inside); })
        .on_press([&] { ++presses; }).on_release([&] { ++releases; });
    auto check = root.checkbox("Snap", false).on_change([&](bool on) { toggled = on; });
    auto slider = root.slider(0, 1, 0.5F).step(0.25F).on_change([&](float v) { value = v; });
    TFL_CHECK(button.valid() && check.valid() && slider.valid());
    TFL_CHECK(button.text() == "Go" && !check.checked() && slider.value() == 0.5F);
    frame(c);

    const auto at = center(c, button.element());
    TFL_CHECK(c.handle(PointerEvent{.action = PointerAction::move, .position = at}).ok());
    click(c, at);
    TFL_CHECK(clicks == 1 && presses == 1 && releases == 1);
    TFL_CHECK(!hovers.empty() && hovers.front());
    TFL_CHECK(c.focus(button.element()).ok() && c.handle(KeyEvent{.key = Key::space}).ok() &&
              c.handle(KeyEvent{.key = Key::space, .down = false}).ok());
    TFL_CHECK(clicks == 2);

    click(c, center(c, check.element()));
    TFL_CHECK(toggled && check.checked());
    check.checked(false);
    TFL_CHECK(!check.checked() && toggled); // programmatic setters never fire callbacks

    TFL_CHECK(c.focus(slider.element()).ok() && c.handle(KeyEvent{.key = Key::right}).ok());
    TFL_CHECK(value == 0.75F && slider.value() == 0.75F);

    button.enabled(false);
    TFL_CHECK(!button.enabled());
    click(c, center(c, button.element()));
    TFL_CHECK(clicks == 2);
    return 0;
}

int painting_and_overlay() {
    Context c;
    Ui ui{c, text_style};
    TFL_CHECK(c.set_viewport({200, 100}).ok());
    std::vector<std::string> order;
    Size seen;
    auto canvas = ui.root().canvas().height(40);
    canvas.on_paint([&](Painter& p) { order.push_back("fg"); seen = p.size(); })
        .on_paint([&](Painter& p) {
            order.push_back("bg");
            p.rect({0, 0, p.size().width, p.size().height}, Color{1, 0, 0, 1});
        }, PaintLayer::background);
    frame(c);
    TFL_CHECK((order == std::vector<std::string>{"bg", "fg"}));
    TFL_CHECK(seen.width == 200 && seen.height == 40);
    frame(c);
    TFL_CHECK(order.size() == 2); // retained: no repaint without a change
    canvas.invalidate();
    frame(c);
    TFL_CHECK(order.size() == 4);

    auto overlay = ui.overlay();
    TFL_CHECK(overlay.valid() && ui.overlay().element() == overlay.element());
    frame(c);
    const auto bounds = c.absolute_bounds(overlay.element()).value();
    TFL_CHECK(bounds.x == 0 && bounds.y == 0 && bounds.width == 200 && bounds.height == 100);
    return 0;
}

int spacing_tokens() {
    Context c;
    Ui ui{c, text_style};
    auto column = ui.root().column();
    frame(c);
    TFL_CHECK(spacing(column) == 8.0F);
    column.gap(3);
    frame(c);
    TFL_CHECK(spacing(column) == 3.0F);
    TFL_CHECK(c.set_theme_value("gui.spacing", 5.0F).ok());
    auto row = ui.root().row();
    frame(c);
    TFL_CHECK(spacing(row) == 5.0F && spacing(column) == 3.0F);
    auto grid = ui.root().grid({.columns = {Track::weighted(), Track::weighted()}});
    auto fixed = ui.root().grid({.columns = {Track::weighted()}, .column_spacing = 2});
    frame(c);
    TFL_CHECK(spacing(grid) == 5.0F && spacing(fixed) == 2.0F);
    grid.gap(1);
    frame(c);
    TFL_CHECK(spacing(grid) == 1.0F);
    return 0;
}

int error_model() {
    Context c;
    Ui ui{c, text_style};
    std::vector<base::Status> errors;
    ui.set_error_handler([&](const base::Status& status) { errors.push_back(status); });
    auto label = ui.root().label("A");
    auto copy = label;
    label.remove();
    TFL_CHECK(!label.valid() && !copy.valid() && errors.empty());
    copy.text("B").width(10);
    TFL_CHECK(errors.size() == 1 && !ui.last_error().ok());
    TFL_CHECK(copy.text().empty() && errors.size() == 1); // stale is reported once
    auto button = ui.root().button("X");
    button.width(-1);
    TFL_CHECK(errors.size() == 2 && button.valid());
    TFL_CHECK(!ui.adopt(ElementId{}).valid() && errors.size() == 3);
    return 0;
}

int menus_and_dock() {
    Context c;
    Ui ui{c, text_style};
    std::vector<std::string> errors;
    ui.set_error_handler([&](const base::Status& s) { errors.push_back(s.message); });
    TFL_CHECK(c.set_viewport({400, 300}).ok());
    int runs = 0;
    examples::MenuExample menus(ui.root(), [&] { ++runs; });
    std::vector<bool> changes;
    menus.design.on_change([&](bool on) { changes.push_back(on); });
    TFL_CHECK(menus.design.checked());
    TFL_CHECK(c.alive(menus.modes.trigger()) && menus.modes.trigger() != menus.modes.element());
    menus.modes.open();
    frame(c);
    TFL_CHECK(menus.modes.is_open());
    click(c, center(c, menus.design.element()));
    frame(c);
    TFL_CHECK(!menus.modes.is_open() && !menus.design.checked());
    TFL_CHECK((changes == std::vector<bool>{false}));

    auto dock = dock::DockSpace::create(c, ui.root().element());
    TFL_CHECK(dock.ok());
    auto panel = dock.value().panel(ui, "tools", "Tools");
    TFL_CHECK(panel.valid() && panel.button("Inside").valid());
    TFL_CHECK(!dock.value().panel(ui, "tools", "Again").valid() && errors.size() == 1);
    TFL_CHECK(runs == 0);
    return 0;
}

} // namespace

int main() {
    auto device = tests::make_null_context();
    auto engine = tests::require(specialist::TextInterop::create(device.device, {{256, 256}}));
    text_style = {&engine, tests::require(engine.load_font(tests::gui_test_font(), 7)),
                  TextureId{1}};
    if (fluent_callbacks() != 0 || painting_and_overlay() != 0 || spacing_tokens() != 0) {
        return 1;
    }
    return error_model() != 0 || menus_and_dock() != 0 ? 1 : 0;
}
#else
int main() { return 0; }
#endif

#include "test_support.hpp"
#include "truffle/gui/builder.hpp"

#include <stdexcept>
#include <utility>

namespace {
using namespace truffle;
using namespace truffle::gui;

void need(const base::Status& status) {
    if (!status.ok()) { throw std::runtime_error(status.message); }
}

template <typename T>
T need(base::Result<T> result) {
    if (!result.ok()) { throw std::runtime_error(result.status().message); }
    return std::move(result).value();
}

int same_tree(const Context& a, ElementId x, const Context& b, ElementId y) {
    TFL_CHECK(need(a.bounds(x)) == need(b.bounds(y)));
    TFL_CHECK(need(a.layout_params(x)) == need(b.layout_params(y)));
    TFL_CHECK(need(a.property(x, Property::padding)) ==
              need(b.property(y, Property::padding)));
    const auto* sx = a.shapes(x);
    const auto* sy = b.shapes(y);
    TFL_CHECK((sx == nullptr) == (sy == nullptr));
    if (sx != nullptr && sy != nullptr) {
        TFL_CHECK(sx->vertices.size() == sy->vertices.size());
        TFL_CHECK(sx->indices == sy->indices);
    }
    const auto cx = a.children(x);
    const auto cy = b.children(y);
    TFL_CHECK(cx.size() == cy.size());
    for (std::size_t i = 0; i < cx.size() && i < cy.size(); ++i) {
        if (same_tree(a, cx[i], b, cy[i]) != 0) { return 1; }
    }
    return 0;
}

int matches_object_api() {
    int clicks = 0;
    Context built;
    Widget ok;
    Widget scroller;
    const auto root = need(build(built, built.root(), ui::column(8, {
        ui::row(4, {
            ui::button("", [&] { ++clicks; }).width(100).bind(ok),
            ui::checkbox("", true),
        }),
        ui::slider(0, 10, 5).weight(1),
        ui::button("").disabled(),
        ui::panel().size({50, 20}).padding({4, 4, 4, 4}),
        ui::scroll({ui::panel().at({0, 300}).size({10, 10})}).height(60).bind(scroller),
    })));
    TFL_CHECK(ok.alive() && scroller.alive());
    TFL_CHECK(built.children(built.root()).size() == 1);

    Context manual;
    const auto col = need(Widget::create(manual, manual.root()));
    need(manual.set_layout(col.element(),
        std::make_shared<StackLayout>(StackOptions{.axis = Axis::vertical, .spacing = 8})));
    const auto row = need(Widget::create(manual, col.element()));
    need(manual.set_layout(row.element(),
        std::make_shared<StackLayout>(StackOptions{.axis = Axis::horizontal, .spacing = 4})));
    const auto button = need(Widget::create(manual, row.element(),
        {.kind = WidgetKind::button}));
    auto params = need(manual.layout_params(button.element()));
    params.width = 100;
    need(manual.set_layout_params(button.element(), params));
    need(Widget::create(manual, row.element(),
        {.kind = WidgetKind::checkbox, .checked = true}));
    const auto slider = need(Widget::create(manual, col.element(),
        {.kind = WidgetKind::slider, .min = 0, .max = 10, .value = 5}));
    params = need(manual.layout_params(slider.element()));
    params.weight = 1;
    need(manual.set_layout_params(slider.element(), params));
    auto off = need(Widget::create(manual, col.element(), {.kind = WidgetKind::button}));
    need(off.set_enabled(false));
    const auto box = need(Widget::create(manual, col.element()));
    params = need(manual.layout_params(box.element()));
    params.width = 50;
    params.height = 20;
    need(manual.set_layout_params(box.element(), params));
    need(manual.set_property_override(box.element(), Property::padding,
                                                Edges{4, 4, 4, 4}));
    const auto scroll = need(Widget::create(manual, col.element(),
        {.kind = WidgetKind::scroll_area}));
    params = need(manual.layout_params(scroll.element()));
    params.height = 60;
    need(manual.set_layout_params(scroll.element(), params));
    const auto inner = need(Widget::create(manual, scroll.content()));
    params = need(manual.layout_params(inner.element()));
    params.position = {0, 300};
    params.width = 10;
    params.height = 10;
    need(manual.set_layout_params(inner.element(), params));

    for (Context* c : {&built, &manual}) {
        TFL_CHECK(c->set_viewport({300, 400}).ok());
        TFL_CHECK(c->prepare_frame(Context::Clock::now()).status.ok());
    }
    TFL_CHECK(same_tree(built, built.root(), manual, manual.root()) == 0);
    TFL_CHECK(root.element() == built.children(built.root())[0]);

    const auto at = need(built.absolute_bounds(ok.element()));
    const Point p{at.x + 4, at.y + 4};
    TFL_CHECK(built.handle(PointerEvent{.action = PointerAction::down, .position = p,
              .button = PointerButton::primary}).ok());
    TFL_CHECK(built.handle(PointerEvent{.action = PointerAction::up, .position = p,
              .button = PointerButton::primary}).ok());
    TFL_CHECK(clicks == 1);
    return 0;
}

int grid_and_defaults() {
    Context c;
    GridOptions options;
    options.columns = {Track::fixed(40), Track::weighted(1)};
    options.rows = {Track::fixed(30)};
    Widget right;
    need(build(c, c.root(), ui::grid(options, {
        ui::panel().cell({.column = 0}),  // panels keep the theme padding (8)
        ui::panel().cell({.column = 1}).bind(right),
    }).size({200, 30})));
    TFL_CHECK(c.set_viewport({200, 30}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(need(c.bounds(right.element())) == (Rect{48, 8, 144, 30}));

    // Widget defaults survive unless a field is set.
    Widget plain;
    need(build(c, c.root(), ui::slider(0, 1, 0).bind(plain)));
    TFL_CHECK(need(c.layout_params(plain.element())).min == (Size{120, 24}));
    return 0;
}

int failure_rolls_back() {
    Context c;
    Widget first;
    const auto before = c.children(c.root()).size();
    // A text label without a text engine is rejected after earlier siblings exist.
    const auto result = build(c, c.root(), ui::column(0, {
        ui::button("").bind(first),
        ui::label("needs text"),
    }));
    TFL_CHECK(!result.ok());
    TFL_CHECK(c.children(c.root()).size() == before);
    TFL_CHECK(!first.alive() && !first.element().valid());
    return 0;
}

} // namespace

int main() {
    return matches_object_api() + grid_and_defaults() + failure_rolls_back();
}

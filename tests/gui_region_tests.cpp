#include "test_support.hpp"
#include "truffle/gui/region.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace truffle;
using namespace truffle::gui;

template <typename T>
T need(base::Result<T> result) {
    if (!result.ok()) { throw std::runtime_error(result.status().message); }
    return std::move(result).value();
}

FrameStats frame(Context& c) {
    return c.prepare_frame(Context::Clock::now());
}

void click(Context& c, ElementId element) {
    const auto at = need(c.absolute_bounds(element));
    const Point p{at.x + 2, at.y + 2};
    (void)c.handle(PointerEvent{.action = PointerAction::down, .position = p,
                                .button = PointerButton::primary});
    (void)c.handle(PointerEvent{.action = PointerAction::up, .position = p,
                                .button = PointerButton::primary});
}

struct Model {
    bool show_extra = false;
    bool first_extra = false;
    bool enabled = false;
    float level = 0.5F;
    int clicks = 0;
    int runs = 0;
    ElementId button;
    ElementId check;
};

Region::Body body(Model& m) {
    return [&m](RegionScope& ui) {
        ++m.runs;
        if (m.show_extra && m.first_extra) { ui.label("extra", ""); }
        ui.row("controls", 4, [&] {
            if (ui.button("ok", "")) { ++m.clicks; }
            m.button = ui.last();
            ui.checkbox("enabled", "", m.enabled);
            m.check = ui.last();
        });
        ui.slider("level", m.level, 0, 1);
        if (m.show_extra && !m.first_extra) { ui.label("extra", ""); }
    };
}

int identity_and_polling() {
    Context c;
    Model m;
    auto region = need(Region::create(c, c.root(), body(m), {.spacing = 2}));
    TFL_CHECK(c.set_viewport({300, 200}).ok());
    auto stats = frame(c);
    TFL_CHECK(stats.status.ok() && stats.updates == 1 && m.runs == 1);
    const auto items = std::vector<ElementId>(c.children(region.element()).begin(),
                                              c.children(region.element()).end());
    TFL_CHECK(items.size() == 2);
    const auto button = m.button;
    const auto count = c.element_count();

    // Nothing pending: no rerun and no frame needed.
    TFL_CHECK(!c.needs_frame(Context::Clock::now()));
    TFL_CHECK(frame(c).updates == 0 && m.runs == 1);

    // An invalidated rerun with unchanged output keeps identity and shapes.
    TFL_CHECK(region.invalidate().ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    stats = frame(c);
    TFL_CHECK(stats.updates == 1 && m.runs == 2);
    TFL_CHECK(stats.shapes.regenerated == 0 && stats.layout.measured == 0);
    TFL_CHECK(m.button == button && c.element_count() == count);
    TFL_CHECK(c.children(region.element())[0] == items[0]);

    // A click reruns the region once and polls true exactly once.
    click(c, m.button);
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    TFL_CHECK(frame(c).updates == 1 && m.clicks == 1);
    TFL_CHECK(region.invalidate().ok());
    TFL_CHECK(frame(c).updates == 1 && m.clicks == 1 && m.button == button);

    // Two-way checkbox: a user toggle writes the model; the model drives the widget.
    click(c, m.check);
    TFL_CHECK(frame(c).updates == 1 && m.enabled);
    m.enabled = false;
    TFL_CHECK(region.invalidate().ok());
    stats = frame(c);
    TFL_CHECK(stats.shapes.regenerated > 0 && !m.enabled);
    return 0;
}

int structure_changes() {
    Context c;
    Model m;
    auto region = need(Region::create(c, c.root(), body(m)));
    TFL_CHECK(c.set_viewport({300, 200}).ok());
    TFL_CHECK(frame(c).status.ok());
    const auto count = c.element_count();
    const auto controls = c.children(region.element())[0];
    const auto button = m.button;

    m.show_extra = true;
    TFL_CHECK(region.invalidate().ok());
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(c.element_count() == count + 1 && c.children(region.element()).size() == 3);

    // Moving an item to the front keeps every element and reorders the tree.
    m.first_extra = true;
    TFL_CHECK(region.invalidate().ok());
    auto stats = frame(c);
    TFL_CHECK(stats.status.ok() && c.element_count() == count + 1);
    const auto order = c.children(region.element());
    TFL_CHECK(order.size() == 3 && order[1] == controls && m.button == button);

    // Items no longer described are destroyed.
    m.show_extra = false;
    TFL_CHECK(region.invalidate().ok());
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(c.element_count() == count && c.children(region.element())[0] == controls);
    return 0;
}

int live_and_errors() {
    Context c;
    int runs = 0;
    bool duplicate = false;
    auto region = need(Region::create(c, c.root(), [&](RegionScope& ui) {
        ++runs;
        ui.label("a", "");
        if (duplicate) { ui.label("a", ""); }
    }, {.live = true}));
    TFL_CHECK(frame(c).updates == 1);
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    TFL_CHECK(frame(c).updates == 1 && runs == 2);
    TFL_CHECK(region.set_live(false).ok());
    TFL_CHECK(!c.needs_frame(Context::Clock::now()) && frame(c).updates == 0);

    duplicate = true;
    TFL_CHECK(region.invalidate().ok());
    TFL_CHECK(!frame(c).status.ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now())); // failed update stays pending
    duplicate = false;
    TFL_CHECK(frame(c).status.ok() && !c.needs_frame(Context::Clock::now()));

    // The updater dies with its element.
    TFL_CHECK(c.destroy(region.element()).ok());
    TFL_CHECK(!region.invalidate().ok() && frame(c).updates == 0);
    TFL_CHECK(!Region{}.invalidate().ok());
    return 0;
}

} // namespace

int main() {
    return identity_and_polling() + structure_changes() + live_and_errors();
}

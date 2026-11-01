#include "test_support.hpp"
#include "truffle/gui/dock/dock.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace truffle;
using namespace truffle::gui;
using namespace truffle::gui::dock;

template <typename T>
T need(base::Result<T> result) {
    if (!result.ok()) { throw std::runtime_error(result.status().message); }
    return std::move(result).value();
}

void need(const base::Status& status) {
    if (!status.ok()) { throw std::runtime_error(status.message); }
}

FrameStats frame(Context& c) {
    return c.prepare_frame(Context::Clock::now());
}

base::Status press(Context& c, PointerAction action, Point at) {
    return c.handle(PointerEvent{.action = action, .position = at,
                                 .button = PointerButton::primary});
}

int layout_model() {
    DockNode layout;
    TFL_CHECK(validate(layout).ok() && layout.empty());
    need(insert_panel(layout, "a", "", DropSide::center));
    need(insert_panel(layout, "b", "a", DropSide::right));
    need(insert_panel(layout, "c", "b", DropSide::center));
    need(insert_panel(layout, "d", "a", DropSide::bottom));
    TFL_CHECK(validate(layout).ok());
    TFL_CHECK(serialize(layout) ==
              R"((split h 5000 5000 (split v 5000 5000 (tabs 0 "a") (tabs 0 "d")) (tabs 1 "b" "c")))");
    TFL_CHECK((panel_ids(layout) == std::vector<std::string>{"a", "d", "b", "c"}));

    // Inserting beside an area in a split of the same axis halves that area.
    need(move_panel(layout, "c", "b", DropSide::left));
    TFL_CHECK(serialize(layout) == R"((split h 5000 2500 2500 (split v 5000 5000 (tabs 0 "a") (tabs 0 "d")) (tabs 0 "c") (tabs 0 "b")))");
    // Emptied areas disappear and single-child splits collapse.
    need(remove_panel(layout, "d"));
    TFL_CHECK(serialize(layout) == R"((split h 5000 2500 2500 (tabs 0 "a") (tabs 0 "c") (tabs 0 "b")))");
    need(move_panel(layout, "c", "a", DropSide::center));
    need(activate_panel(layout, "a"));
    TFL_CHECK(serialize(layout) == R"((split h 6667 3333 (tabs 0 "a" "c") (tabs 0 "b")))");
    const DockNode before = layout;
    TFL_CHECK(!move_panel(layout, "a", "a", DropSide::left).ok());
    TFL_CHECK(!move_panel(layout, "a", "missing", DropSide::left).ok());
    TFL_CHECK(!insert_panel(layout, "b", "a", DropSide::left).ok());
    TFL_CHECK(layout == before);

    // Text round trip, escapes and rejected input.
    need(insert_panel(layout, "say \"hi\" \\", "b", DropSide::top));
    const auto text = serialize(layout);
    const auto parsed = need(parse(text));
    TFL_CHECK(serialize(parsed) == text && contains(parsed, "say \"hi\" \\"));
    TFL_CHECK(!parse("(tabs 0 \"a\" \"a\")").ok());
    TFL_CHECK(!parse("(tabs 1 \"a\")").ok());
    TFL_CHECK(!parse("(split h 1 (tabs 0 \"a\"))").ok());
    TFL_CHECK(!parse("(split h 1 0 (tabs 0 \"a\") (tabs 0 \"b\"))").ok());
    TFL_CHECK(!parse("(tabs 0 \"a\") x").ok());
    TFL_CHECK(!parse("(tabs 0 \"a").ok());
    TFL_CHECK(!parse(std::string(100, '(')).ok());

    DockPresets presets;
    need(presets.set("Wide", layout));
    need(presets.set("Single", DockNode::tabs({"a"})));
    TFL_CHECK(!presets.set("", layout).ok());
    TFL_CHECK(!presets.set("Bad", DockNode::tabs({"a", "a"})).ok());
    const auto saved = presets.serialize();
    const auto loaded = need(DockPresets::parse(saved));
    TFL_CHECK(loaded.serialize() == saved && loaded.size() == 2);
    TFL_CHECK(loaded.find("Wide") != nullptr &&
              serialize(*loaded.find("Wide")) == serialize(*presets.find("Wide")));
    TFL_CHECK(!DockPresets::parse("dock-presets 2\n").ok());
    TFL_CHECK(!DockPresets::parse("dock-presets 1\npreset \"x\" (tabs 0 \"a\")\n"
                                  "preset \"x\" (tabs 0 \"a\")\n").ok());
    return 0;
}

int dock_space() {
    Context c;
    TFL_CHECK(c.set_viewport({400, 300}).ok());
    auto dock = need(DockSpace::create(c, c.root()));
    LayoutParams params;
    params.width = 400;
    params.height = 300;
    need(c.set_layout_params(dock.element(), params));
    const ElementId a = need(c.create(c.root()));
    const ElementId b = need(c.create(c.root()));
    const ElementId d = need(c.create(c.root()));
    need(dock.add_panel("a", "A", a));
    need(dock.add_panel("b", "B", b));
    need(dock.add_panel("c", "C", d));
    TFL_CHECK(!dock.add_panel("a", "", need(c.create(c.root()))).ok());
    TFL_CHECK(!dock.set_layout(DockNode::tabs({"missing"})).ok());
    need(dock.restore(R"((split h 5000 5000 (tabs 0 "a") (tabs 0 "b" "c")))"));
    TFL_CHECK(frame(c).status.ok());

    // Areas split the 396 px left by the 4 px divider; tabs share the strip.
    TFL_CHECK(need(c.bounds(a)) == (Rect{0, 24, 198, 276}));
    TFL_CHECK(need(c.bounds(dock.dividers()[0])) == (Rect{198, 0, 4, 300}));
    TFL_CHECK(need(c.bounds(b)) == (Rect{202, 24, 198, 276}));
    TFL_CHECK(need(c.bounds(dock.tab("c"))) == (Rect{301, 0, 99, 24}));
    TFL_CHECK(need(c.parent(b)) == dock.element() && need(c.parent(d)) != dock.element());
    TFL_CHECK(c.hit_test({250, 100}) != d);

    // Pressing a tab shows its panel; the same content element is reused.
    need(press(c, PointerAction::down, {350, 12}));
    need(press(c, PointerAction::up, {350, 12}));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.parent(d)) == dock.element() && need(c.parent(b)) != dock.element());
    TFL_CHECK(need(c.bounds(d)) == (Rect{202, 24, 198, 276}));

    // Dragging tab "c" to the left edge of area "a" previews, then splits.
    need(press(c, PointerAction::down, {350, 12}));
    need(press(c, PointerAction::move, {352, 13}));
    TFL_CHECK(!dock.drop_preview()); // under the drag threshold
    need(press(c, PointerAction::move, {20, 150}));
    auto preview = dock.drop_preview();
    TFL_CHECK(preview && preview->panel == "c" && preview->anchor == "a");
    TFL_CHECK(preview->side == DropSide::left && preview->rect == (Rect{0, 0, 99, 300}));
    need(press(c, PointerAction::move, {100, 150}));
    TFL_CHECK(dock.drop_preview()->side == DropSide::center);
    need(press(c, PointerAction::move, {100, 290}));
    TFL_CHECK(dock.drop_preview()->side == DropSide::bottom);
    TFL_CHECK(frame(c).status.ok());
    need(press(c, PointerAction::move, {20, 150}));
    TFL_CHECK(frame(c).status.ok());
    need(press(c, PointerAction::up, {20, 150}));
    TFL_CHECK(!dock.drop_preview());
    TFL_CHECK(dock.save() == R"((split h 2500 2500 5000 (tabs 0 "c") (tabs 0 "a") (tabs 0 "b")))");
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(dock.dividers().size() == 2);
    TFL_CHECK(need(c.bounds(d)) == (Rect{0, 24, 98, 276}));
    TFL_CHECK(need(c.bounds(a)) == (Rect{102, 24, 98, 276}));
    TFL_CHECK(need(c.parent(b)) == dock.element());

    // Dragging a divider resizes its neighbours, never below the minimum.
    need(press(c, PointerAction::down, {100, 150}));
    need(press(c, PointerAction::move, {140, 150}));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.bounds(d)).width == 138 && need(c.bounds(a)).width == 58);
    need(press(c, PointerAction::move, {390, 150}));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.bounds(a)).width == 48 && need(c.bounds(d)).width == 148);
    need(press(c, PointerAction::up, {390, 150}));
    TFL_CHECK(validate(dock.layout()).ok());

    // Presets and restore keep the same panel elements.
    need(dock.save_preset("three"));
    need(dock.restore(R"((tabs 1 "a" "b"))"));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.parent(b)) == dock.element() && need(c.parent(d)) != dock.element());
    TFL_CHECK(dock.dividers().empty() && need(c.bounds(b)) == (Rect{0, 24, 400, 276}));
    TFL_CHECK(!dock.restore("(tabs 0 \"unknown\")").ok() && !dock.load_preset("none").ok());
    need(dock.load_preset("three"));
    TFL_CHECK(frame(c).status.ok() && dock.dividers().size() == 2);
    TFL_CHECK(need(c.bounds(a)).width == 48);
    const auto presets = need(DockPresets::parse(dock.presets().serialize()));
    TFL_CHECK(presets.find("three") != nullptr && serialize(*presets.find("three")) == dock.save());

    // Removing a panel hands its content back.
    need(dock.remove_panel("b", c.root()));
    TFL_CHECK(need(c.parent(b)) == c.root() && !contains(dock.layout(), "b"));
    TFL_CHECK(frame(c).status.ok() && dock.dividers().size() == 1);
    need(dock.undock("a"));
    need(dock.dock("a", "c", DropSide::top));
    TFL_CHECK(dock.save() == R"((split v 5000 5000 (tabs 0 "a") (tabs 0 "c")))");
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.bounds(dock.dividers()[0])) == (Rect{0, 148, 400, 4}));

    const auto count = c.element_count();
    TFL_CHECK(c.destroy(dock.element()).ok() && !dock.alive() && !dock.activate("a").ok());
    TFL_CHECK(c.element_count() < count);
    return 0;
}

} // namespace

int main() {
    try {
        return layout_model() + dock_space();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

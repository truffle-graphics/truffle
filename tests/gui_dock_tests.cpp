#include "test_support.hpp"
#include "truffle/gui/dock/dock.hpp"
#include "truffle/gui/themes.hpp"

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
    params.width = 400.0F;
    params.height = 300.0F;
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

struct CursorHost final : Host {
    Cursor cursor = Cursor::arrow;
    void set_cursor(Cursor value) override { cursor = value; }
};

struct WindowHost final : DockWindowHost {
    enum class Mode { accept, refuse, fail, invalid };
    Mode mode = Mode::accept;
    std::unique_ptr<Surface> surface;
    int opened = 0;
    int closed = 0;
    bool shown = false;
    CursorHost services;
    base::Result<Surface*> open_window(Context& c, const DockWindowRequest&) override {
        ++opened;
        if (mode == Mode::refuse || mode == Mode::fail) {
            return base::Status::failure(mode == Mode::refuse ? base::StatusCode::unsupported
                                                               : base::StatusCode::backend_error,
                                          "test native host refusal/failure");
        }
        if (mode == Mode::invalid) { return static_cast<Surface*>(nullptr); }
        surface = need(Surface::create(c));
        surface->set_host(&services);
        need(surface->set_viewport({240, 160}, 2));
        return surface.get();
    }
    void show_window(std::string_view) noexcept override { shown = true; }
    void close_window(std::string_view) noexcept override { ++closed; surface.reset(); shown = false; }
};

int native_lifecycle() {
    // Host outlives Context: Context teardown is itself a native cleanup path.
    WindowHost windows;
    CursorHost cursor;
    Context c;
    c.set_host(&cursor);
    need(c.set_viewport({400, 300}));
    auto dock = need(DockSpace::create(c, c.root()));
    need(c.set_placement(dock.element(), Rect{0, 0, 400, 300}));
    const auto a = need(c.create(c.root()));
    const auto b = need(c.create(c.root()));
    need(c.set_decoration(a, {.fill = {1, 0, 0, 1}}));
    int clicks = 0;
    bool refuse_focus_loss = false;
    need(c.set_interaction(a, {.focusable = true}));
    need(c.set_event_handler(a, [&](Context&, RoutedEvent& event) {
        if (const auto* focus = std::get_if<ElementFocusEvent>(&event.payload);
            focus != nullptr && !focus->focused && refuse_focus_loss) {
            event.status = base::Status::failure(base::StatusCode::backend_error,
                                                  "application refused focus loss");
        }
        if (const auto* p = event.input<PointerEvent>(); p && p->action == PointerAction::down) {
            ++clicks;
        }
    }));
    need(dock.add_panel("a", "Live panel", a));
    need(dock.add_panel("b", "Other panel", b));
    need(dock.set_layout(DockNode::split(dock::Axis::horizontal,
                                        {DockNode::tabs({"a"}), DockNode::tabs({"b"})})));
    TFL_CHECK(frame(c).status.ok());
    const auto original = dock.save();
    const Rect rect{450, 30, 240, 160};
    TFL_CHECK(dock.detach("a", rect).code == base::StatusCode::unsupported);
    TFL_CHECK(dock.save() == original && need(c.parent(a)) == dock.element());
    need(dock.set_window_host(&windows));
    for (auto mode : {WindowHost::Mode::refuse, WindowHost::Mode::fail, WindowHost::Mode::invalid}) {
        windows.mode = mode;
        TFL_CHECK(!dock.detach("a", rect).ok());
        TFL_CHECK(dock.save() == original && !dock.detached("a"));
        TFL_CHECK(need(c.parent(a)) == dock.element() && c.alive(a));
    }
    windows.mode = WindowHost::Mode::accept;
    need(c.focus(a));
    refuse_focus_loss = true;
    TFL_CHECK(!dock.detach("a", rect).ok());
    TFL_CHECK(!windows.surface && dock.save() == original &&
              need(c.parent(a)) == dock.element());
    refuse_focus_loss = false;
    need(dock.detach("a", rect));
    TFL_CHECK(dock.detached("a") && windows.shown && !contains(dock.layout(), "a"));
    TFL_CHECK(!dock.set_window_host(nullptr).ok());
    TFL_CHECK(dock.save() == original && need(c.parent(a)) == windows.surface->root());
    TFL_CHECK(!c.focus(a).ok() && !c.capture_pointer(a).ok());
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.bounds(a)) == (Rect{0, 0, 240, 160}));
    DrawData primary, secondary;
    assemble(c, {{400, 300}}, primary);
    windows.surface->assemble({{480, 320}, 2}, secondary);
    TFL_CHECK(!secondary.vertices.empty());
    need(windows.surface->handle(FocusEvent{true}));
    need(windows.surface->focus(a));
    bool view_update_seen = false;
    need(c.set_updater(a, [&] {
        view_update_seen = c.root() == windows.surface->root() && c.focused() == a &&
                           c.viewport() == (Size{240, 160}) && c.scale() == 2 &&
                           &c.host() == &windows.services;
        return base::Status::success();
    }));
    TFL_CHECK(frame(c).status.ok() && view_update_seen && c.root() != windows.surface->root());
    need(windows.surface->handle(PointerEvent{.action = PointerAction::down,
                                              .position = {20, 20},
                                              .button = PointerButton::primary}));
    TFL_CHECK(clicks == 1 && !c.input().pressed(PointerButton::primary));
    refuse_focus_loss = true;
    const auto detached_layout = dock.layout();
    TFL_CHECK(!dock.redock("a").ok());
    TFL_CHECK(dock.detached("a") && windows.surface && dock.layout() == detached_layout &&
              need(c.parent(a)) == windows.surface->root());
    refuse_focus_loss = false;
    need(windows.surface->set_viewport({280, 190}, 1));
    TFL_CHECK(frame(c).status.ok() && need(c.bounds(a)) == (Rect{0, 0, 280, 190}));
    need(dock.redock("a"));
    TFL_CHECK(!windows.surface && !dock.detached("a") && dock.save() == original);
    TFL_CHECK(frame(c).status.ok() && need(c.parent(a)) == dock.element());
    need(press(c, PointerAction::down, {20, 60}));
    need(press(c, PointerAction::up, {20, 60}));
    TFL_CHECK(clicks == 2);

    // Restore redocks without recreating the application subtree or state.
    need(dock.detach("a", rect));
    const auto saved = dock.save();
    need(dock.restore(saved));
    TFL_CHECK(!windows.surface && c.alive(a) && clicks == 2 && dock.save() == saved);
    need(dock.detach("a", rect));
    need(dock.shutdown());
    TFL_CHECK(!windows.surface && !dock.detached("a"));
    need(dock.detach("a", rect));
    need(c.destroy(dock.element()));
    TFL_CHECK(!windows.surface && !c.alive(a) && !dock.alive());
    return 0;
}

struct InputHost final : Host {
    std::optional<Rect> area;
    void set_text_input_area(std::optional<Rect> value) override { area = value; }
};

int surface_input_ownership() {
    InputHost primary, secondary;
    Context c;
    c.set_host(&primary);
    need(c.set_viewport({400, 300}));
    auto surface = need(Surface::create(c));
    surface->set_host(&secondary);
    need(surface->set_viewport({240, 160}, 2));
    const auto main = need(c.create(c.root()));
    const auto content = need(c.create(c.root()));
    need(c.set_interaction(main, {.focusable = true}));
    need(c.set_interaction(content, {.focusable = true}));
    need(c.focus(main));
    need(surface->attach(content));
    need(surface->focus(content));
    TFL_CHECK(c.focused() == main && &c.host() == &primary);
    secondary.area = Rect{1, 2, 3, 4};
    bool lost = false;
    need(c.set_event_handler(content, [&](Context& current, RoutedEvent& event) {
        if (const auto* focus = std::get_if<ElementFocusEvent>(&event.payload);
            focus != nullptr && !focus->focused) {
            lost = &current.host() == &secondary;
            current.host().set_text_input_area(std::nullopt);
        }
    }));
    need(c.set_interaction(content, {.enabled = false, .focusable = true}));
    TFL_CHECK(lost && !secondary.area && c.focused() == main);
    need(c.set_interaction(content, {.focusable = true}));
    need(surface->focus(content));
    secondary.area = Rect{1, 2, 3, 4};
    need(c.destroy(content));
    TFL_CHECK(!secondary.area && c.focused() == main && &c.host() == &primary);
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(!c.needs_frame(Context::Clock::now()));
    need(surface->set_viewport({240, 160}, 1));
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    return 0;
}

struct ManyWindows final : DockWindowHost {
    std::map<std::string, std::unique_ptr<Surface>, std::less<>> windows;
    int closed = 0;
    base::Result<Surface*> open_window(Context& c, const DockWindowRequest& request) override {
        auto surface = need(Surface::create(c));
        need(surface->set_viewport({200, 150}));
        auto* result = surface.get();
        windows.emplace(request.panel, std::move(surface));
        return result;
    }
    void show_window(std::string_view) noexcept override {}
    void close_window(std::string_view panel) noexcept override {
        const auto found = windows.find(panel);
        if (found != windows.end()) { windows.erase(found); ++closed; }
    }
};

int multiple_windows() {
    ManyWindows host;
    {
        Context c;
        auto dock = need(DockSpace::create(c, c.root()));
        for (const auto* id : {"a", "b", "c"}) {
            need(dock.add_panel(id, id, need(c.create(c.root()))));
        }
        need(dock.restore(
            R"((split h 3000 7000 (tabs 0 "a") (split v 6000 4000 (tabs 0 "b") (tabs 0 "c"))))"));
        need(dock.set_window_host(&host));
        const auto home = dock.save();
        const Rect rect{0, 0, 200, 150};
        for (const auto* id : {"c", "a", "b"}) { need(dock.detach(id, rect)); }
        TFL_CHECK(dock.layout().empty() && host.windows.size() == 3 && dock.save() == home);
        need(dock.redock("a"));
        TFL_CHECK(host.windows.size() == 2 && dock.detached("b") && dock.detached("c"));
        need(dock.redock("c"));
        TFL_CHECK(host.windows.size() == 1 && dock.detached("b"));
        need(dock.redock("b"));
        TFL_CHECK(host.windows.empty() && dock.save() == home);
        need(dock.detach("a", rect));
        need(dock.restore(home));
        TFL_CHECK(host.windows.empty() && dock.save() == home);
        // Implicit Context teardown must release all native resources too.
        need(dock.detach("b", rect));
    }
    TFL_CHECK(host.windows.empty() && host.closed == 5);
    return 0;
}

int divider_cursors() {
    CursorHost host;
    Context c;
    c.set_host(&host);
    need(c.set_viewport({400, 300}));
    auto dock = need(DockSpace::create(c, c.root()));
    need(c.set_placement(dock.element(), Rect{0, 0, 400, 300}));
    need(dock.add_panel("a", "A", need(c.create(c.root()))));
    need(dock.add_panel("b", "B", need(c.create(c.root()))));
    for (const auto axis : {dock::Axis::horizontal, dock::Axis::vertical}) {
        need(dock.set_layout(DockNode::split(axis,
                                           {DockNode::tabs({"a"}), DockNode::tabs({"b"})})));
        TFL_CHECK(frame(c).status.ok());
        const auto bounds = need(c.bounds(dock.dividers()[0]));
        const Point at{bounds.x + 1, bounds.y + 1};
        const auto wanted = axis == dock::Axis::horizontal ? Cursor::resize_horizontal : Cursor::resize_vertical;
        need(press(c, PointerAction::move, at));
        TFL_CHECK(host.cursor == wanted);
        need(c.handle(PointerEvent{.action = PointerAction::leave}));
        TFL_CHECK(host.cursor == Cursor::arrow);
        need(press(c, PointerAction::down, at));
        need(press(c, PointerAction::move, {500, 500}));
        TFL_CHECK(host.cursor == wanted);
        need(press(c, PointerAction::cancel, {500, 500}));
        TFL_CHECK(host.cursor == Cursor::arrow && !c.captured().valid());
        need(press(c, PointerAction::down, at));
        TFL_CHECK(host.cursor == wanted);
        need(c.handle(FocusEvent{false}));
        TFL_CHECK(host.cursor == Cursor::arrow && !c.captured().valid());
        need(c.handle(FocusEvent{true}));
        need(press(c, PointerAction::move, {20, 60}));
        TFL_CHECK(host.cursor == Cursor::arrow);
    }
    return 0;
}

int theme_and_gap() {
    Context c;
    TFL_CHECK(c.set_viewport({400, 300}).ok());
    DockSpaceOptions bad;
    bad.metrics.gap = -1;
    TFL_CHECK(!DockSpace::create(c, c.root(), bad).ok());
    DockSpaceOptions options;
    options.metrics.gap = 6;
    auto dock = need(DockSpace::create(c, c.root(), options));
    LayoutParams params;
    params.width = 400.0F;
    params.height = 300.0F;
    need(c.set_layout_params(dock.element(), params));
    const ElementId a = need(c.create(c.root()));
    const ElementId b = need(c.create(c.root()));
    need(dock.add_panel("a", "A", a));
    need(dock.add_panel("b", "B", b));
    need(dock.restore(R"((split h 5000 5000 (tabs 0 "a") (tabs 0 "b")))"));
    TFL_CHECK(frame(c).status.ok());
    // 400 - (4 divider + 6 gap) = 390 shared; the splitter is centred in the gap.
    TFL_CHECK(need(c.bounds(a)) == (Rect{0, 24, 195, 276}));
    TFL_CHECK(need(c.bounds(dock.dividers()[0])) == (Rect{198, 0, 4, 300}));
    TFL_CHECK(need(c.bounds(b)) == (Rect{205, 24, 195, 276}));

    const auto grey = [](float v, float alpha = 1) { return Color{v, v, v, alpha}; };
    TFL_CHECK(need(c.decoration(dock.element())).fill == grey(0.07F));
    TFL_CHECK(need(c.decoration(dock.tab("a"))).fill == grey(0.24F));
    need(apply_theme(c, BuiltinTheme::truffle_light));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(need(c.decoration(dock.element())).fill == grey(0.84F));
    TFL_CHECK(need(c.decoration(dock.tab("a"))).fill == grey(1.0F));
    TFL_CHECK(need(c.decoration(dock.dividers()[0])).fill == grey(0.80F));

    need(dock.restore(R"((tabs 0 "a" "b"))"));
    ComponentStyle tabs;
    tabs.fill.normal = Color{1, 0, 0, 1};
    tabs.fill.checked = Color{0, 0, 1, 1};
    need(c.set_component_style(Component::dock_tab, tabs));
    ComponentStyle selected;
    selected.fill.checked = Color{0, 1, 0, 1};
    need(c.set_component_style(dock.tab("b"), selected));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK((need(c.decoration(dock.tab("a"))).fill == Color{0, 0, 1, 1}));
    TFL_CHECK((need(c.decoration(dock.tab("b"))).fill == Color{1, 0, 0, 1}));
    need(dock.activate("b"));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK((need(c.decoration(dock.tab("a"))).fill == Color{1, 0, 0, 1}));
    TFL_CHECK((need(c.decoration(dock.tab("b"))).fill == Color{0, 1, 0, 1}));
    return 0;
}

} // namespace

int main() {
    try {
        return layout_model() + dock_space() + native_lifecycle() + multiple_windows() +
               surface_input_ownership() + divider_cursors() + theme_and_gap();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

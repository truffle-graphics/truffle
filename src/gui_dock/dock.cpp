#include "truffle/gui/dock/dock.hpp"

#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>
#include <vector>

namespace truffle::gui::dock {
namespace {

[[nodiscard]] base::Status invalid(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

[[nodiscard]] base::Status dead_dock() { return invalid("GUI dock space is not alive"); }

[[nodiscard]] bool inside(Rect r, Point p) noexcept {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

[[nodiscard]] float along(Point p, Axis axis) noexcept {
    return axis == Axis::horizontal ? p.x : p.y;
}

const DockNode& at(const DockNode& root, std::span<const std::size_t> path) {
    const DockNode* node = &root;
    for (const auto index : path) { node = &node->children[index]; }
    return *node;
}

DockNode& at(DockNode& root, std::span<const std::size_t> path) {
    DockNode* node = &root;
    for (const auto index : path) { node = &node->children[index]; }
    return *node;
}

base::Status style(Context& context, ElementId element, Component component,
                   std::initializer_list<Property> properties) {
    auto status = context.set_component(element, component);
    for (const auto property : properties) {
        if (status.ok()) { status = context.bind_component_property(element, property, element); }
    }
    return status;
}

} // namespace

struct DockSpace::State : std::enable_shared_from_this<DockSpace::State> {
    struct Panel {
        std::string id;
        std::string title;
        ElementId content;
        ElementId tab;
        Surface* window = nullptr;
        DockNode home;
    };
    struct Area {
        std::vector<std::size_t> path;
        Rect rect;
    };
    struct Divider {
        std::vector<std::size_t> path; // of the split
        std::size_t index = 0;          // between children `index` and `index + 1`
        Axis axis = Axis::horizontal;
        float available = 0;            // split extent shared by the fractions
    };
    struct Drag {
        std::string panel;
        Point start;
        bool moving = false;
        std::optional<DropPreview> preview;
    };
    struct Resize {
        std::size_t divider = 0;
        float last = 0;
    };

    Context* context = nullptr;
    ElementId root;
    ElementId stash;
    ElementId marker;
    DockSpaceOptions options;
    std::vector<Panel> panels;
    DockNode layout;
    DockPresets presets;
    std::vector<Area> areas;
    std::vector<ElementId> backgrounds;
    std::vector<Divider> dividers;
    std::vector<ElementId> divider_elements;
    std::optional<Drag> drag;
    std::optional<Resize> resize;
    DockWindowHost* host = nullptr;
    bool transferring = false;

    [[nodiscard]] bool live() const noexcept { return context != nullptr && context->alive(root); }

    Panel* find(std::string_view id) noexcept {
        const auto found = std::ranges::find(panels, id, &Panel::id);
        return found == panels.end() ? nullptr : &*found;
    }

    void collect(const DockNode& node, std::vector<std::size_t>& path) {
        if (node.kind == DockNode::Kind::tabs) {
            if (!node.panels.empty()) { areas.push_back({path, {}}); }
            return;
        }
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            path.push_back(i);
            collect(node.children[i], path);
            path.pop_back();
            if (i + 1 < node.children.size()) {
                dividers.push_back({path, i, node.axis, 0});
            }
        }
    }

    base::Status pool(std::vector<ElementId>& elements, std::size_t size, bool divider) {
        while (elements.size() > size) {
            if (auto status = context->destroy(elements.back()); !status.ok()) { return status; }
            elements.pop_back();
        }
        while (elements.size() < size) {
            auto created = context->create(root);
            if (!created.ok()) { return created.status(); }
            const auto id = created.value();
            elements.push_back(id);
            auto status = style(*context, id,
                                divider ? Component::dock_divider : Component::dock_area,
                                {Property::fill, Property::border_color, Property::border_width,
                                 Property::corner_radius});
            if (status.ok()) { status = context->set_interaction(id, {.hit_test = divider}); }
            if (status.ok() && divider) {
                status = context->set_event_handler(
                    id, [self = shared_from_this()](Context&, RoutedEvent& event) {
                        self->on_divider(event);
                    });
            }
            if (!status.ok()) { return status; }
        }
        return base::Status::success();
    }

    // Rebuilds the structure for `layout`: one background per tabbed area, one
    // element per divider, then orders the dock's children for drawing.
    base::Status apply() {
        // Areas keep their last rectangles until arranged again, so input
        // between a change and the next frame still finds them.
        const auto previous = std::move(areas);
        areas.clear();
        dividers.clear();
        std::vector<std::size_t> path;
        collect(layout, path);
        for (auto& area : areas) {
            const auto kept = std::ranges::find(previous, area.path, &Area::path);
            if (kept != previous.end()) { area.rect = kept->rect; }
        }
        if (auto status = pool(backgrounds, areas.size(), false); !status.ok()) { return status; }
        if (auto status = pool(divider_elements, dividers.size(), true); !status.ok()) { return status; }

        std::size_t order = 0;
        const auto show = [&](ElementId element) { return context->reparent(element, root, order++); };
        auto status = show(stash);
        for (const auto background : backgrounds) {
            if (status.ok()) { status = show(background); }
        }
        std::vector<const Panel*> shown;
        for (const auto& area : areas) {
            const DockNode& tabs = at(layout, area.path);
            if (Panel* panel = find(tabs.panels[tabs.active]); panel != nullptr && status.ok()) {
                status = show(panel->content);
                shown.push_back(panel);
            }
        }
        for (const auto& area : areas) {
            const DockNode& tabs = at(layout, area.path);
            for (std::size_t i = 0; i < tabs.panels.size() && status.ok(); ++i) {
                const Panel* panel = find(tabs.panels[i]);
                status = show(panel->tab);
                if (status.ok()) {
                    auto tab_state = context->component_state(panel->tab);
                    if (!tab_state.ok()) { return tab_state.status(); }
                    tab_state.value().checked = i == tabs.active;
                    status = context->set_component_state(panel->tab, tab_state.value());
                }
            }
        }
        for (const auto divider : divider_elements) {
            if (status.ok()) { status = show(divider); }
        }
        if (status.ok()) { status = show(marker); }
        for (const auto& panel : panels) {
            if (!status.ok()) { break; }
            if (panel.window == nullptr && std::ranges::find(shown, &panel) == shown.end()) {
                status = context->reparent(panel.content, stash);
            }
            if (status.ok() && !contains(layout, panel.id)) {
                status = context->reparent(panel.tab, stash);
            }
        }
        if (!status.ok()) { return status; }
        return context->invalidate(root, Dirty::arrange);
    }

    DockNode restore_one(DockNode result, const Panel& panel) const {
        DockNode candidate = panel.home;
        for (const auto& id : panel_ids(candidate)) {
            if (id != panel.id && !contains(result, id)) {
                (void)dock::remove_panel(candidate, id);
            }
        }
        auto without = candidate;
        (void)dock::remove_panel(without, panel.id);
        if (result == without) { return candidate; }
        const auto ids = panel_ids(result);
        (void)insert_panel(result, panel.id, ids.empty() ? "" : ids.front(), DropSide::center);
        return result;
    }

    DockNode restored_layout() const {
        DockNode result = layout;
        for (const auto& panel : panels) {
            if (panel.window != nullptr) { result = restore_one(std::move(result), panel); }
        }
        return result;
    }

    void cleanup_windows() {
        transferring = true;
        resize.reset();
        context->host().set_cursor(Cursor::arrow);
        for (auto& panel : panels) {
            if (panel.window == nullptr) { continue; }
            // Destruction cannot be refused by a focus handler. The dock
            // still owns the live subtree and must reclaim it before closing.
            (void)panel.window->release_input();
            (void)context->reparent(panel.content, stash);
            panel.window = nullptr;
            host->close_window(panel.id);
        }
        transferring = false;
    }

    base::Status detach_panel(std::string_view id, Rect rect) {
        if (transferring) { return invalid("GUI dock transfer is already in progress"); }
        Panel* panel = find(id);
        if (panel == nullptr || panel->window != nullptr || !contains(layout, id)) {
            return invalid("GUI dock can detach only a docked registered panel");
        }
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
            !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
            rect.width <= 0 || rect.height <= 0) {
            return invalid("GUI dock detached window rectangle must be finite and positive");
        }
        if (host == nullptr) {
            return base::Status::failure(base::StatusCode::unsupported,
                                         "GUI dock has no native window host");
        }
        DockNode next = layout;
        const DockNode home = restored_layout();
        if (auto status = dock::remove_panel(next, id); !status.ok()) { return status; }
        transferring = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{transferring};
        auto opened = host->open_window(*context, {panel->id, panel->title, rect});
        if (!opened.ok()) { return opened.status(); }
        Surface* window = opened.value();
        if (window == nullptr || &window->context() != context || !window->alive() ||
            !context->children(window->root()).empty()) {
            host->close_window(id);
            return invalid("GUI dock host returned an invalid or nonempty surface");
        }
        auto status = window->attach(panel->content);
        if (!status.ok()) { host->close_window(id); return status; }
        const DockNode before = layout;
        panel->window = window;
        panel->home = home;
        layout = std::move(next);
        status = apply();
        if (!status.ok()) {
            layout = before;
            panel->window = nullptr;
            (void)context->reparent(panel->content, stash);
            (void)apply();
            host->close_window(id);
            return status;
        }
        host->show_window(id);
        return base::Status::success();
    }

    base::Status replace_layout(DockNode next) {
        if (transferring) { return invalid("GUI dock transfer is already in progress"); }
        transferring = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{transferring};
        // Release input before moving a live focused/captured subtree.
        for (auto& panel : panels) {
            if (panel.window != nullptr) {
                if (auto status = panel.window->release_input(); !status.ok()) { return status; }
            }
        }
        const auto before = layout;
        layout = std::move(next);
        auto status = apply();
        if (!status.ok()) {
            layout = before;
            for (auto& panel : panels) {
                if (panel.window != nullptr) { (void)panel.window->attach(panel.content); }
            }
            (void)apply();
            return status;
        }
        for (auto& panel : panels) {
            if (panel.window == nullptr) { continue; }
            if (!contains(layout, panel.id)) {
                status = context->reparent(panel.content, stash);
                if (!status.ok()) { return status; }
            }
            panel.window = nullptr;
            host->close_window(panel.id);
        }
        drag.reset();
        resize.reset();
        context->host().set_cursor(Cursor::arrow);
        return base::Status::success();
    }

    void put(LayoutPass& pass, ElementId element, Rect rect) {
        rect.width = std::max(0.0F, rect.width);
        rect.height = std::max(0.0F, rect.height);
        (void)pass.measure(element, Constraints::tight(rect.size()));
        pass.arrange(element, rect);
    }

    void place(LayoutPass& pass, const DockNode& node, Rect rect, std::size_t& area,
               std::size_t& divider) {
        const auto& metrics = options.metrics;
        if (node.kind == DockNode::Kind::tabs) {
            if (node.panels.empty()) { return; }
            areas[area].rect = rect;
            put(pass, backgrounds[area++], rect);
            const float count = static_cast<float>(node.panels.size());
            const float width = std::min(metrics.max_tab_width, rect.width / count);
            const float strip = std::min(metrics.tab_height, rect.height);
            for (std::size_t i = 0; i < node.panels.size(); ++i) {
                put(pass, find(node.panels[i])->tab,
                    {rect.x + static_cast<float>(i) * width, rect.y, width, strip});
            }
            put(pass, find(node.panels[node.active])->content,
                {rect.x, rect.y + strip, rect.width, rect.height - strip});
            return;
        }
        const bool across = node.axis == Axis::horizontal;
        const float extent = across ? rect.width : rect.height;
        const float spacing = metrics.divider + metrics.gap;
        const float gaps = spacing * static_cast<float>(node.children.size() - 1);
        const float available = std::max(0.0F, extent - gaps);
        const float end = (across ? rect.x : rect.y) + extent;
        float position = across ? rect.x : rect.y;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const bool last = i + 1 == node.children.size();
            const float size = last ? std::max(0.0F, end - position)
                                    : std::round(available * node.fractions[i]);
            const Rect child = across ? Rect{position, rect.y, size, rect.height}
                                      : Rect{rect.x, position, rect.width, size};
            place(pass, node.children[i], child, area, divider);
            position += size;
            if (!last) {
                dividers[divider].available = available;
                // The splitter is centred in its spacing; the gap either side
                // leaves the dock background visible between areas.
                const float at = position + metrics.gap * 0.5F;
                put(pass, divider_elements[divider++],
                    across ? Rect{at, rect.y, metrics.divider, rect.height}
                           : Rect{rect.x, at, rect.width, metrics.divider});
                position += spacing;
            }
        }
    }

    void arrange(LayoutPass& pass, Rect content) {
        put(pass, stash, {});
        std::size_t area = 0;
        std::size_t divider = 0;
        place(pass, layout, content, area, divider);
        put(pass, marker, drag && drag->preview ? drag->preview->rect : Rect{});
    }

    [[nodiscard]] std::optional<DropPreview> preview_at(std::string_view panel, Point p) const {
        for (const auto& area : areas) {
            const Rect r = area.rect;
            if (!inside(r, p)) { continue; }
            const DockNode& tabs = at(layout, area.path);
            const auto anchor = std::ranges::find_if(tabs.panels, [&](const std::string& id) {
                return id != panel;
            });
            if (anchor == tabs.panels.end()) { return std::nullopt; }
            DropPreview preview{std::string(panel), *anchor, DropSide::center, r};
            if (p.y < r.y + options.metrics.tab_height) { return preview; }
            const float u = (p.x - r.x) / r.width;
            const float v = (p.y - r.y) / r.height;
            const float nearest = std::min({u, 1 - u, v, 1 - v});
            if (nearest >= 0.25F) { return preview; }
            if (nearest == u) {
                preview.side = DropSide::left;
                preview.rect.width = r.width / 2;
            } else if (nearest == 1 - u) {
                preview.side = DropSide::right;
                preview.rect = {r.x + r.width / 2, r.y, r.width / 2, r.height};
            } else if (nearest == v) {
                preview.side = DropSide::top;
                preview.rect.height = r.height / 2;
            } else {
                preview.side = DropSide::bottom;
                preview.rect = {r.x, r.y + r.height / 2, r.width, r.height / 2};
            }
            return preview;
        }
        return std::nullopt;
    }

    [[nodiscard]] Point local(Point p) const {
        const auto origin = context->absolute_bounds(root);
        return origin.ok() ? Point{p.x - origin.value().x, p.y - origin.value().y} : p;
    }

    base::Status tab_input(const std::string& panel, ElementId tab, const PointerEvent& pointer,
                           RoutedEvent& event) {
        const Point p = local(pointer.position);
        switch (pointer.action) {
        case PointerAction::down: {
            if (pointer.button != PointerButton::primary) { return base::Status::success(); }
            event.prevent_default();
            if (contains(layout, panel)) {
                const DockNode before = layout;
                if (auto status = activate_panel(layout, panel); !status.ok()) { return status; }
                if (!(layout == before)) {
                    if (auto status = apply(); !status.ok()) { return status; }
                }
            }
            drag = Drag{panel, p, false, std::nullopt};
            return context->capture_pointer(tab, pointer.pointer);
        }
        case PointerAction::move: {
            if (!drag || drag->panel != panel) { return base::Status::success(); }
            if (!drag->moving && std::hypot(p.x - drag->start.x, p.y - drag->start.y) <
                                     options.metrics.drag_threshold) {
                return base::Status::success();
            }
            drag->moving = true;
            drag->preview = preview_at(panel, p);
            event.prevent_default();
            return context->invalidate(root, Dirty::arrange);
        }
        case PointerAction::up:
        case PointerAction::cancel: {
            if (!drag || drag->panel != panel) { return base::Status::success(); }
            const Drag finished = std::move(*drag);
            drag.reset();
            (void)context->release_pointer(pointer.pointer);
            if (pointer.action == PointerAction::up && finished.moving && finished.preview) {
                auto status = move_panel(layout, panel, finished.preview->anchor,
                                         finished.preview->side);
                if (!status.ok()) { return status; }
                return apply();
            }
            if (pointer.action == PointerAction::up && finished.moving) {
                const auto bounds = context->bounds(root);
                if (bounds.ok() && !inside({0, 0, bounds.value().width, bounds.value().height}, p)) {
                    return detach_panel(panel, {p.x, p.y, std::max(240.0F, bounds.value().width / 2),
                                                std::max(160.0F, bounds.value().height / 2)});
                }
            }
            return context->invalidate(root, Dirty::arrange);
        }
        default: return base::Status::success();
        }
    }

    void on_divider(RoutedEvent& event) {
        if (std::holds_alternative<CaptureLostEvent>(event.payload)) {
            resize.reset();
            context->host().set_cursor(Cursor::arrow);
            return;
        }
        const auto element = std::ranges::find(divider_elements, event.current);
        if (element == divider_elements.end()) { return; }
        const auto index = static_cast<std::size_t>(element - divider_elements.begin());
        const Divider& divider = dividers[index];
        const auto cursor = divider.axis == Axis::horizontal ? Cursor::resize_horizontal
                                                              : Cursor::resize_vertical;
        if (const auto* hover = std::get_if<HoverEvent>(&event.payload)) {
            context->host().set_cursor(hover->entered || resize ? cursor : Cursor::arrow);
            return;
        }
        const auto* pointer = event.input<PointerEvent>();
        if (pointer == nullptr) { return; }
        const float position = along(local(pointer->position), divider.axis);
        switch (pointer->action) {
        case PointerAction::down:
            if (pointer->button != PointerButton::primary) { return; }
            resize = Resize{index, position};
            context->host().set_cursor(cursor);
            event.prevent_default();
            event.status = context->capture_pointer(divider_elements[index], pointer->pointer);
            return;
        case PointerAction::move: {
            context->host().set_cursor(cursor);
            if (!resize || resize->divider != index || !(divider.available > 0)) { return; }
            DockNode& split = at(layout, divider.path);
            float& first = split.fractions[divider.index];
            float& second = split.fractions[divider.index + 1];
            const float a = first * divider.available;
            const float b = second * divider.available;
            const float low = options.metrics.min_area - a;
            const float high = b - options.metrics.min_area;
            const float delta = low > high ? 0.0F : std::clamp(position - resize->last, low, high);
            if (delta == 0) { return; }
            first = (a + delta) / divider.available;
            second = (b - delta) / divider.available;
            resize->last += delta;
            event.prevent_default();
            event.status = context->invalidate(root, Dirty::arrange);
            return;
        }
        case PointerAction::up:
        case PointerAction::cancel:
            if (resize && resize->divider == index) {
                resize.reset();
                (void)context->release_pointer(pointer->pointer);
            }
            context->host().set_cursor(pointer->action == PointerAction::up &&
                                               context->hovered() == event.current
                                           ? cursor : Cursor::arrow);
            return;
        default: return;
        }
    }
};

namespace {

class DockLayout final : public Layout {
public:
    explicit DockLayout(std::shared_ptr<DockSpace::State> state) : state_(std::move(state)) {}

    Size measure(LayoutPass&, ElementId, Constraints content) const override {
        return content.clamp({});
    }
    void arrange(LayoutPass& pass, ElementId, Rect content) const override {
        state_->arrange(pass, content);
    }

private:
    std::shared_ptr<DockSpace::State> state_;
};

} // namespace

base::Result<DockSpace> DockSpace::create(Context& context, ElementId parent,
                                          DockSpaceOptions options) {
    if (!std::isfinite(options.metrics.gap) || options.metrics.gap < 0) {
        return invalid("GUI dock gap must be finite and non-negative");
    }
    if (auto status = install_default_theme(context); !status.ok()) { return status; }
    auto created = context.create(parent);
    if (!created.ok()) { return created.status(); }
    auto state = std::make_shared<State>();
    state->context = &context;
    state->root = created.value();
    state->options = std::move(options);
    const auto fail = [&](base::Status status) -> base::Result<DockSpace> {
        const auto cleanup = context.destroy(state->root);
        return cleanup.ok() ? status : cleanup;
    };
    auto stash = context.create(state->root);
    if (!stash.ok()) { return fail(stash.status()); }
    state->stash = stash.value();
    auto marker = context.create(state->root);
    if (!marker.ok()) { return fail(marker.status()); }
    state->marker = marker.value();
    auto status = context.set_interaction(state->stash, {.hit_test = false, .enabled = false});
    if (status.ok()) { status = context.set_interaction(state->marker, {.hit_test = false}); }
    if (status.ok()) {
        status = style(context, state->root, Component::dock_background,
                       {Property::fill, Property::border_color, Property::border_width,
                        Property::corner_radius, Property::padding});
    }
    if (status.ok()) {
        status = style(context, state->marker, Component::dock_marker,
                       {Property::fill, Property::border_color, Property::border_width,
                        Property::corner_radius});
    }
    if (status.ok()) { status = context.set_overlay(state->marker, true); }
    if (status.ok()) { status = context.set_layout(state->root, std::make_shared<DockLayout>(state)); }
    if (status.ok()) {
        status = context.set_cleanup(state->root, [state] {
            if (state->live()) { state->cleanup_windows(); }
        });
    }
    if (status.ok()) { status = state->apply(); }
    if (!status.ok()) { return fail(status); }
    return DockSpace(std::move(state));
}

ElementId DockSpace::element() const noexcept { return state_ ? state_->root : ElementId{}; }
bool DockSpace::alive() const noexcept { return state_ && state_->live(); }

base::Status DockSpace::add_panel(std::string id, std::string title, ElementId content) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    auto& s = *state_;
    if (id.empty()) { return invalid("GUI dock panel ids must not be empty"); }
    if (s.find(id) != nullptr) { return invalid("GUI dock panel '" + id + "' already exists"); }
    if (!s.context->alive(content) || content == s.root) {
        return invalid("GUI dock panel content must be a live element");
    }
    auto tab = s.context->create(s.stash);
    if (!tab.ok()) { return tab.status(); }
    const auto tab_id = tab.value();
    auto status = s.context->reparent(content, s.stash);
    if (status.ok()) {
        status = style(*s.context, tab_id, Component::dock_tab,
                       {Property::fill, Property::border_color, Property::border_width,
                        Property::corner_radius, Property::padding, Property::text_color,
                        Property::font_size});
    }
    if (status.ok() && s.options.text_engine != nullptr && !title.empty()) {
        status = s.context->set_text_source(tab_id, *s.options.text_engine, s.options.font, title,
                                            s.options.atlas);
    }
    if (status.ok()) {
        status = s.context->set_event_handler(
            tab_id, [state = state_, id, tab_id](Context&, RoutedEvent& event) {
                if (const auto* hover = std::get_if<HoverEvent>(&event.payload)) {
                    auto current = state->context->component_state(tab_id);
                    if (!current.ok()) { event.status = current.status(); return; }
                    current.value().hovered = hover->entered;
                    event.status = state->context->set_component_state(tab_id, current.value());
                    return;
                }
                if (std::holds_alternative<CaptureLostEvent>(event.payload)) {
                    if (state->drag && state->drag->panel == id) {
                        state->drag.reset();
                        (void)state->context->invalidate(state->root, Dirty::arrange);
                    }
                    return;
                }
                if (const auto* pointer = event.input<PointerEvent>()) {
                    if (auto result = state->tab_input(id, tab_id, *pointer, event); !result.ok()) {
                        event.status = result;
                    }
                }
            });
    }
    if (!status.ok()) {
        (void)s.context->destroy(tab_id);
        return status;
    }
    s.panels.push_back({std::move(id), std::move(title), content, tab_id, nullptr, {}});
    return base::Status::success();
}

base::Status DockSpace::remove_panel(std::string_view id, ElementId new_parent) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    auto& s = *state_;
    State::Panel* panel = s.find(id);
    if (panel == nullptr) { return invalid("GUI dock panel is not registered"); }
    if (!s.context->alive(new_parent)) { return invalid("GUI dock panel needs a live new parent"); }
    if (panel->window != nullptr) {
        if (auto status = redock(id); !status.ok()) { return status; }
    }
    if (contains(s.layout, id)) {
        if (auto status = dock::remove_panel(s.layout, id); !status.ok()) { return status; }
    }
    if (auto status = s.context->reparent(panel->content, new_parent); !status.ok()) { return status; }
    if (s.drag && s.drag->panel == id) { s.drag.reset(); }
    const auto tab = panel->tab;
    s.panels.erase(s.panels.begin() + (panel - s.panels.data()));
    if (auto status = s.context->destroy(tab); !status.ok()) { return status; }
    return s.apply();
}

base::Status DockSpace::set_layout(DockNode layout) {
    if (!alive()) { return dead_dock(); }
    if (auto status = validate(layout); !status.ok()) { return status; }
    for (const auto& id : panel_ids(layout)) {
        if (state_->find(id) == nullptr) {
            return invalid("GUI dock layout names unregistered panel '" + id + "'");
        }
    }
    return state_->replace_layout(std::move(layout));
}

const DockNode& DockSpace::layout() const noexcept {
    static const DockNode empty;
    return state_ ? state_->layout : empty;
}

base::Status DockSpace::dock(std::string_view panel, std::string_view anchor, DropSide side) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    if (state_->find(panel) == nullptr) { return invalid("GUI dock panel is not registered"); }
    if (detached(panel)) {
        state_->transferring = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{state_->transferring};
        DockNode next = state_->layout;
        auto status = insert_panel(next, panel, anchor, side);
        if (!status.ok()) { return status; }
        // Moving one detached panel must not close unrelated native windows.
        auto* found = state_->find(panel);
        if (auto released = found->window->release_input(); !released.ok()) { return released; }
        auto* window = found->window;
        const auto before = state_->layout;
        state_->layout = std::move(next);
        auto applied = state_->apply();
        if (!applied.ok()) {
            state_->layout = before;
            (void)window->attach(found->content);
            (void)state_->apply();
            return applied;
        }
        found->window = nullptr;
        state_->host->close_window(panel);
        return base::Status::success();
    }
    auto status = contains(state_->layout, panel)
                      ? move_panel(state_->layout, panel, anchor, side)
                      : insert_panel(state_->layout, panel, anchor, side);
    return status.ok() ? state_->apply() : status;
}

base::Status DockSpace::undock(std::string_view panel) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    if (detached(panel)) { return invalid("Redock a native panel before hiding it"); }
    auto status = dock::remove_panel(state_->layout, panel);
    return status.ok() ? state_->apply() : status;
}

base::Status DockSpace::activate(std::string_view panel) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    auto status = activate_panel(state_->layout, panel);
    return status.ok() ? state_->apply() : status;
}

std::string DockSpace::save() const {
    return serialize(state_ ? state_->restored_layout() : layout());
}

base::Status DockSpace::restore(std::string_view text) {
    auto parsed = parse(text);
    if (!parsed.ok()) { return parsed.status(); }
    return set_layout(std::move(parsed).value());
}

DockPresets& DockSpace::presets() noexcept {
    static DockPresets detached;
    return state_ ? state_->presets : detached;
}

base::Status DockSpace::save_preset(std::string name) {
    if (!alive()) { return dead_dock(); }
    return state_->presets.set(std::move(name), state_->restored_layout());
}

base::Status DockSpace::load_preset(std::string_view name) {
    if (!alive()) { return dead_dock(); }
    const DockNode* preset = state_->presets.find(name);
    if (preset == nullptr) { return invalid("GUI dock preset is not defined"); }
    return set_layout(*preset);
}

std::optional<DropPreview> DockSpace::drop_preview() const {
    if (!state_ || !state_->drag) { return std::nullopt; }
    return state_->drag->preview;
}

ElementId DockSpace::tab(std::string_view panel) const noexcept {
    if (!state_) { return {}; }
    const State::Panel* found = state_->find(panel);
    return found == nullptr ? ElementId{} : found->tab;
}

std::span<const ElementId> DockSpace::dividers() const noexcept {
    if (!state_) { return {}; }
    return state_->divider_elements;
}

base::Status DockSpace::set_window_host(DockWindowHost* host) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    if (host != state_->host && std::ranges::any_of(state_->panels, [](const State::Panel& p) {
            return p.window != nullptr;
        })) { return invalid("GUI dock cannot replace a host with open windows"); }
    state_->host = host;
    return base::Status::success();
}

base::Status DockSpace::detach(std::string_view panel, Rect rect) {
    return alive() ? state_->detach_panel(panel, rect) : dead_dock();
}

bool DockSpace::detached(std::string_view panel) const noexcept {
    const auto* found = state_ ? state_->find(panel) : nullptr;
    return found != nullptr && found->window != nullptr;
}

base::Status DockSpace::redock(std::string_view panel) {
    if (!alive()) { return dead_dock(); }
    if (state_->transferring) { return invalid("GUI dock transfer is already in progress"); }
    const auto* found = state_->find(panel);
    if (found == nullptr || found->window == nullptr) {
        return invalid("GUI dock panel has no native window");
    }
    {
        state_->transferring = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{state_->transferring};
        auto* window = found->window;
        if (auto status = window->release_input(); !status.ok()) { return status; }
        const auto before = state_->layout;
        state_->layout = state_->restore_one(state_->layout, *found);
        auto status = state_->apply();
        if (!status.ok()) {
            state_->layout = before;
            (void)window->attach(found->content);
            (void)state_->apply();
            return status;
        }
        state_->find(panel)->window = nullptr;
        state_->host->close_window(panel);
        return base::Status::success();
    }
}

base::Status DockSpace::shutdown() {
    return alive() ? state_->replace_layout(state_->restored_layout()) : dead_dock();
}

} // namespace truffle::gui::dock

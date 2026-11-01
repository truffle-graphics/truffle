#include "truffle/gui/dock/dock.hpp"

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

} // namespace

struct DockSpace::State : std::enable_shared_from_this<DockSpace::State> {
    struct Panel {
        std::string id;
        std::string title;
        ElementId content;
        ElementId tab;
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
            auto status = context->set_decoration(id, divider ? options.style.divider_fill
                                                              : options.style.area);
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
                    status = context->set_decoration(panel->tab, i == tabs.active
                                                                     ? options.style.active_tab
                                                                     : options.style.tab);
                }
            }
        }
        for (const auto divider : divider_elements) {
            if (status.ok()) { status = show(divider); }
        }
        if (status.ok()) { status = show(marker); }
        for (const auto& panel : panels) {
            if (!status.ok()) { break; }
            if (std::ranges::find(shown, &panel) == shown.end()) {
                status = context->reparent(panel.content, stash);
            }
            if (status.ok() && !contains(layout, panel.id)) {
                status = context->reparent(panel.tab, stash);
            }
        }
        if (!status.ok()) { return status; }
        return context->invalidate(root, Dirty::arrange);
    }

    void put(LayoutPass& pass, ElementId element, Rect rect) {
        rect.width = std::max(0.0F, rect.width);
        rect.height = std::max(0.0F, rect.height);
        (void)pass.measure(element, Constraints::tight(rect.size()));
        pass.arrange(element, rect);
    }

    void place(LayoutPass& pass, const DockNode& node, Rect rect, std::size_t& area,
               std::size_t& divider) {
        const auto& style = options.style;
        if (node.kind == DockNode::Kind::tabs) {
            if (node.panels.empty()) { return; }
            areas[area].rect = rect;
            put(pass, backgrounds[area++], rect);
            const float count = static_cast<float>(node.panels.size());
            const float width = std::min(style.max_tab_width, rect.width / count);
            const float strip = std::min(style.tab_height, rect.height);
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
        const float gaps = style.divider * static_cast<float>(node.children.size() - 1);
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
                put(pass, divider_elements[divider++],
                    across ? Rect{position, rect.y, style.divider, rect.height}
                           : Rect{rect.x, position, rect.width, style.divider});
                position += style.divider;
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
            if (p.y < r.y + options.style.tab_height) { return preview; }
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
                                     options.style.drag_threshold) {
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
            return context->invalidate(root, Dirty::arrange);
        }
        default: return base::Status::success();
        }
    }

    void on_divider(RoutedEvent& event) {
        if (std::holds_alternative<CaptureLostEvent>(event.payload)) {
            resize.reset();
            return;
        }
        const auto* pointer = event.input<PointerEvent>();
        if (pointer == nullptr) { return; }
        const auto element = std::ranges::find(divider_elements, event.current);
        if (element == divider_elements.end()) { return; }
        const auto index = static_cast<std::size_t>(element - divider_elements.begin());
        const Divider& divider = dividers[index];
        const float position = along(local(pointer->position), divider.axis);
        switch (pointer->action) {
        case PointerAction::down:
            if (pointer->button != PointerButton::primary) { return; }
            resize = Resize{index, position};
            event.prevent_default();
            event.status = context->capture_pointer(divider_elements[index], pointer->pointer);
            return;
        case PointerAction::move: {
            if (!resize || resize->divider != index || !(divider.available > 0)) { return; }
            DockNode& split = at(layout, divider.path);
            float& first = split.fractions[divider.index];
            float& second = split.fractions[divider.index + 1];
            const float a = first * divider.available;
            const float b = second * divider.available;
            const float low = options.style.min_area - a;
            const float high = b - options.style.min_area;
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
    if (status.ok()) { status = context.set_decoration(state->marker, state->options.style.marker); }
    if (status.ok()) { status = context.set_overlay(state->marker, true); }
    if (status.ok()) { status = context.set_layout(state->root, std::make_shared<DockLayout>(state)); }
    if (status.ok()) { status = state->apply(); }
    if (!status.ok()) { return fail(status); }
    return DockSpace(std::move(state));
}

ElementId DockSpace::element() const noexcept { return state_ ? state_->root : ElementId{}; }
bool DockSpace::alive() const noexcept { return state_ && state_->live(); }

base::Status DockSpace::add_panel(std::string id, std::string title, ElementId content) {
    if (!alive()) { return dead_dock(); }
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
    LayoutParams params;
    params.padding = {8, 4, 8, 4};
    if (status.ok()) { status = s.context->set_layout_params(tab_id, params); }
    if (status.ok()) { status = s.context->set_decoration(tab_id, s.options.style.tab); }
    if (status.ok() && s.options.text_engine != nullptr && !title.empty()) {
        status = s.context->set_text_source(tab_id, *s.options.text_engine, s.options.font, title,
                                            s.options.atlas);
    }
    if (status.ok()) {
        status = s.context->set_event_handler(
            tab_id, [state = state_, id, tab_id](Context&, RoutedEvent& event) {
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
    s.panels.push_back({std::move(id), std::move(title), content, tab_id});
    return base::Status::success();
}

base::Status DockSpace::remove_panel(std::string_view id, ElementId new_parent) {
    if (!alive()) { return dead_dock(); }
    auto& s = *state_;
    State::Panel* panel = s.find(id);
    if (panel == nullptr) { return invalid("GUI dock panel is not registered"); }
    if (!s.context->alive(new_parent)) { return invalid("GUI dock panel needs a live new parent"); }
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
    state_->layout = std::move(layout);
    state_->drag.reset();
    state_->resize.reset();
    return state_->apply();
}

const DockNode& DockSpace::layout() const noexcept {
    static const DockNode empty;
    return state_ ? state_->layout : empty;
}

base::Status DockSpace::dock(std::string_view panel, std::string_view anchor, DropSide side) {
    if (!alive()) { return dead_dock(); }
    if (state_->find(panel) == nullptr) { return invalid("GUI dock panel is not registered"); }
    auto status = contains(state_->layout, panel)
                      ? move_panel(state_->layout, panel, anchor, side)
                      : insert_panel(state_->layout, panel, anchor, side);
    return status.ok() ? state_->apply() : status;
}

base::Status DockSpace::undock(std::string_view panel) {
    if (!alive()) { return dead_dock(); }
    auto status = dock::remove_panel(state_->layout, panel);
    return status.ok() ? state_->apply() : status;
}

base::Status DockSpace::activate(std::string_view panel) {
    if (!alive()) { return dead_dock(); }
    auto status = activate_panel(state_->layout, panel);
    return status.ok() ? state_->apply() : status;
}

std::string DockSpace::save() const { return serialize(layout()); }

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
    return state_->presets.set(std::move(name), state_->layout);
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

void DockSpace::set_window_host(DockWindowHost* host) noexcept {
    if (state_) { state_->host = host; }
}

} // namespace truffle::gui::dock

#include "truffle/gui/collections.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status invalid_list() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI virtual list is not alive");
}

// Gives each row the full content width at its `position.y`. Reads the height
// the list last used and asks for an update when the arranged height differs.
class RowsLayout final : public Layout {
public:
    RowsLayout(float row_height, std::shared_ptr<const float> used)
        : row_height_(row_height), used_(std::move(used)) {}

    Size measure(LayoutPass& pass, ElementId element, Constraints content) const override {
        float width = 0;
        for (const auto child : pass.children(element)) {
            width = std::max(width, pass.measure(child, {{0, row_height_},
                                                         {content.max.width, row_height_}}).width);
        }
        return content.clamp({width, 0});
    }

    void arrange(LayoutPass& pass, ElementId element, Rect content) const override {
        for (const auto child : pass.children(element)) {
            pass.arrange(child, {content.x, content.y + pass.params(child).position.y,
                                 content.width, row_height_});
        }
        if (content.height != *used_) { pass.request_update(element); }
    }

private:
    float row_height_;
    std::shared_ptr<const float> used_;
};

} // namespace

struct VirtualList::State {
    struct Row {
        ElementId element;
        std::size_t index = npos;
    };

    Context* context = nullptr;
    ElementId element;
    VirtualListDesc desc;
    double offset = 0;
    float height = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    std::vector<Row> rows;
    std::shared_ptr<float> used = std::make_shared<float>(-1.0F);

    [[nodiscard]] bool live() const noexcept { return context != nullptr && context->alive(element); }
    [[nodiscard]] double extent() const noexcept {
        return static_cast<double>(desc.count) * desc.row_height;
    }
    [[nodiscard]] double max_offset() const noexcept {
        return std::max(0.0, extent() - static_cast<double>(height));
    }

    base::Status update() {
        const auto bounds = context->bounds(element);
        const auto params = context->layout_params(element);
        if (!bounds.ok()) { return bounds.status(); }
        if (!params.ok()) { return params.status(); }
        height = std::max(0.0F, bounds.value().height - params.value().padding.vertical());
        *used = height;
        const double row = desc.row_height;
        offset = std::clamp(offset, 0.0, max_offset());
        first = height > 0 ? std::min(desc.count, static_cast<std::size_t>(offset / row)) : 0;
        const auto visible = height > 0
            ? static_cast<std::size_t>(std::ceil(static_cast<double>(height) / row)) + 1 : 0;
        last = std::min(desc.count, first + visible);

        for (auto& entry : rows) {
            if (entry.index < first || entry.index >= last) { entry.index = npos; }
        }
        const std::size_t needed = last - first;
        while (rows.size() > needed) {
            const auto spare = std::ranges::find(rows, npos, &Row::index);
            const auto status = context->destroy(spare->element);
            if (!status.ok()) { return status; }
            rows.erase(spare);
        }
        while (rows.size() < needed) {
            const auto created = context->create(element);
            if (!created.ok()) { return created.status(); }
            rows.push_back({created.value()});
            if (desc.make_row) {
                const auto status = desc.make_row(*context, created.value());
                if (!status.ok()) { return status; }
            }
        }
        std::vector<bool> shown(needed, false);
        for (const auto& entry : rows) {
            if (entry.index != npos) { shown[entry.index - first] = true; }
        }
        std::size_t missing = 0;
        for (auto& entry : rows) {
            if (entry.index == npos) {
                while (shown[missing]) { ++missing; }
                entry.index = first + missing++;
                if (desc.bind_row) {
                    const auto status = desc.bind_row(*context, entry.element, entry.index);
                    if (!status.ok()) { entry.index = npos; return status; }
                }
            }
            auto placed = context->layout_params(entry.element);
            if (!placed.ok()) { return placed.status(); }
            const float y = static_cast<float>(static_cast<double>(entry.index - first) * row);
            if (placed.value().position.y != y) {
                placed.value().position.y = y;
                const auto status = context->set_layout_params(entry.element, placed.value());
                if (!status.ok()) { return status; }
            }
        }
        return context->set_child_offset(element,
            {0, -static_cast<float>(offset - static_cast<double>(first) * row)});
    }

    base::Status scroll_by(double requested, RoutedEvent& event) {
        const double next = std::clamp(requested, 0.0, max_offset());
        if (next == offset) { return base::Status::success(); }
        offset = next;
        event.prevent_default();
        event.stop_propagation(); // at an edge the parent may scroll instead
        return context->request_update(element);
    }

    base::Status handle(RoutedEvent& event) {
        if (const auto* pointer = event.input<PointerEvent>()) {
            if (pointer->action != PointerAction::down ||
                pointer->button != PointerButton::primary) {
                return base::Status::success();
            }
            const auto at = context->absolute_bounds(element);
            const auto params = context->layout_params(element);
            if (!at.ok() || !params.ok()) { return base::Status::success(); }
            const double y = static_cast<double>(pointer->position.y - at.value().y -
                                                 params.value().padding.top) + offset;
            const auto status = context->focus(element);
            if (y >= 0 && desc.activated) {
                const auto index = static_cast<std::size_t>(y / desc.row_height);
                if (index < desc.count) { desc.activated(index); }
            }
            event.prevent_default();
            return status;
        }
        if (const auto* wheel = event.input<WheelEvent>()) {
            const double unit = wheel->precise ? 1.0 : 20.0;
            return scroll_by(offset - static_cast<double>(wheel->delta_y) * unit, event);
        }
        if (const auto* key = event.input<KeyEvent>(); key != nullptr && key->down) {
            switch (key->key) {
            case Key::down: return scroll_by(offset + desc.row_height, event);
            case Key::up: return scroll_by(offset - desc.row_height, event);
            case Key::page_down: return scroll_by(offset + height, event);
            case Key::page_up: return scroll_by(offset - height, event);
            case Key::home: return scroll_by(0, event);
            case Key::end: return scroll_by(max_offset(), event);
            default: break;
            }
        }
        return base::Status::success();
    }
};

base::Result<VirtualList> VirtualList::create(Context& context, ElementId parent,
                                              VirtualListDesc desc) {
    if (!(desc.row_height > 0) || !std::isfinite(desc.row_height)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI virtual list rows need a positive height");
    }
    auto created = context.create(parent);
    if (!created.ok()) { return created.status(); }
    auto state = std::make_shared<State>();
    state->context = &context;
    state->element = created.value();
    state->desc = std::move(desc);
    const auto id = state->element;
    auto status = context.set_layout(id, std::make_shared<RowsLayout>(state->desc.row_height,
                                                                      state->used));
    if (status.ok()) { status = context.set_interaction(id, {.focusable = true}); }
    if (status.ok()) {
        status = context.set_event_handler(id, [state](Context&, RoutedEvent& event) {
            const auto result = state->handle(event);
            if (!result.ok()) { event.status = result; }
        });
    }
    if (status.ok()) { status = context.set_updater(id, [state] { return state->update(); }); }
    if (!status.ok()) {
        const auto cleanup = context.destroy(id);
        return cleanup.ok() ? status : cleanup;
    }
    return VirtualList(std::move(state));
}

ElementId VirtualList::element() const noexcept { return state_ ? state_->element : ElementId{}; }
bool VirtualList::alive() const noexcept { return state_ && state_->live(); }
std::size_t VirtualList::count() const noexcept { return state_ ? state_->desc.count : 0; }
double VirtualList::scroll() const noexcept { return state_ ? state_->offset : 0.0; }
std::size_t VirtualList::first_visible() const noexcept { return state_ ? state_->first : 0; }
std::size_t VirtualList::last_visible() const noexcept { return state_ ? state_->last : 0; }

base::Status VirtualList::set_count(std::size_t count) {
    if (!alive()) { return invalid_list(); }
    state_->desc.count = count;
    for (auto& entry : state_->rows) { entry.index = npos; }
    return state_->context->request_update(state_->element);
}

base::Status VirtualList::refresh() {
    if (!alive()) { return invalid_list(); }
    for (auto& entry : state_->rows) { entry.index = npos; }
    return state_->context->request_update(state_->element);
}

base::Status VirtualList::refresh(std::size_t index) {
    if (!alive()) { return invalid_list(); }
    for (auto& entry : state_->rows) {
        if (entry.index == index) {
            entry.index = npos;
            return state_->context->request_update(state_->element);
        }
    }
    return base::Status::success();
}

base::Status VirtualList::set_scroll(double offset) {
    if (!alive()) { return invalid_list(); }
    if (!std::isfinite(offset)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI virtual list scroll must be finite");
    }
    state_->offset = offset;
    return state_->context->request_update(state_->element);
}

base::Status VirtualList::scroll_to(std::size_t index) {
    if (!alive()) { return invalid_list(); }
    const double row = state_->desc.row_height;
    const double top = static_cast<double>(index) * row;
    if (top < state_->offset) {
        return set_scroll(top);
    }
    if (top + row > state_->offset + state_->height) {
        return set_scroll(top + row - state_->height);
    }
    return base::Status::success();
}

ElementId VirtualList::row(std::size_t index) const noexcept {
    if (!state_) { return {}; }
    const auto found = std::ranges::find(state_->rows, index, &State::Row::index);
    return found == state_->rows.end() ? ElementId{} : found->element;
}

std::shared_ptr<const Layout> table_columns(std::vector<Track> columns, float spacing) {
    GridOptions options;
    options.columns = std::move(columns);
    options.rows = {Track::weighted(1)};
    options.column_spacing = spacing;
    return std::make_shared<GridLayout>(std::move(options));
}

std::vector<TreeRow> flatten_tree(std::span<const std::uint64_t> roots,
                                  const TreeSource& source) {
    std::vector<TreeRow> rows;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) { stack.emplace_back(*it, 0); }
    std::vector<std::uint64_t> children;
    while (!stack.empty()) {
        const auto [node, depth] = stack.back();
        stack.pop_back();
        children.clear();
        if (source.children) { source.children(node, children); }
        rows.push_back({node, depth, !children.empty()});
        if (!children.empty() && source.expanded && source.expanded(node)) {
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.emplace_back(*it, depth + 1);
            }
        }
    }
    return rows;
}

} // namespace truffle::gui

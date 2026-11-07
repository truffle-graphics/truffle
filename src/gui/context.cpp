#include "truffle/gui/context.hpp"
#include "truffle/gui/surface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace truffle::gui {
namespace {

constexpr Dirty all_dirty =
    Dirty::style | Dirty::measure | Dirty::arrange | Dirty::shape;
constexpr std::uint32_t retired_generation = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] std::uint32_t index_of(ElementId element) noexcept {
    return static_cast<std::uint32_t>(element.value() & 0xFFFF'FFFFU);
}

[[nodiscard]] std::uint32_t generation_of(ElementId element) noexcept {
    return static_cast<std::uint32_t>(element.value() >> 32U);
}

[[nodiscard]] ElementId make_id(std::uint32_t index,
                                std::uint32_t generation) noexcept {
    return ElementId{(static_cast<std::uint64_t>(generation) << 32U) | index};
}

[[nodiscard]] std::uint8_t button_bit(PointerButton button) noexcept {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(button));
}

[[nodiscard]] constexpr Dirty without(Dirty flags) noexcept {
    return static_cast<Dirty>(~static_cast<std::uint8_t>(flags));
}

[[nodiscard]] base::Status invalid_element() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI element handle is not alive");
}

} // namespace

Context::Context() : root_(allocate()), primary_root_(root_) {
    mark(root_, all_dirty);
}

Context::~Context() {
    // Cleanup can close host surfaces and therefore mutate/grow the node pool.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto cleanup = std::exchange(nodes_[i].cleanup, {});
        if (cleanup) { cleanup(); }
    }
}

base::Status Context::set_cleanup(ElementId element, std::function<void()> cleanup) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    node->cleanup = std::move(cleanup);
    return base::Status::success();
}

ElementId Context::root() const noexcept {
    return root_;
}

bool Context::alive(ElementId element) const noexcept {
    return find(element) != nullptr;
}

std::size_t Context::element_count() const noexcept {
    return live_;
}

base::Result<ElementId> Context::create(ElementId parent) {
    if (find(parent) == nullptr) {
        return invalid_element();
    }
    const ElementId element = allocate();
    // allocate() may grow nodes_, so look the parent up again.
    find(parent)->children.push_back(element);
    find(element)->parent = parent;
    if (find(parent)->placed_children) {
        refresh_flow(parent);
    }
    mark(element, all_dirty);
    return element;
}

base::Status Context::destroy(ElementId element) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (element == primary_root_ || std::ranges::any_of(surface_roots_, [element](const auto& entry) {
            return entry.root == element;
        })) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "the GUI root element cannot be destroyed");
    }
    if (auto* surface = surface_of(element); surface != nullptr && root_ != surface->root()) {
        return surface->remove(element);
    }
    auto cleanup = std::exchange(node->cleanup, {});
    if (cleanup) { cleanup(); }
    node = find(element);
    const ElementId parent = node->parent;
    detach(element, *node);
    mark(parent, Dirty::measure);

    std::vector<ElementId> pending{element};
    while (!pending.empty()) {
        const ElementId current = pending.back();
        pending.pop_back();
        auto child_cleanup = std::exchange(find(current)->cleanup, {});
        if (child_cleanup) { child_cleanup(); }
        forget_input(current);
        forget_styles(current);
        if (nodes_[index_of(current)].overlay) {
            --overlays_;
        }
        std::erase_if(updaters_, [current](const UpdaterEntry& entry) {
            return entry.element == current;
        });
        std::erase(popups_, current);
        std::erase(input_filters_, current);
        Node& doomed = nodes_[index_of(current)];
        pending.insert(pending.end(), doomed.children.begin(), doomed.children.end());
        const std::uint32_t generation = doomed.generation + 1;
        doomed = Node{};
        doomed.generation = generation;
        if (doomed.generation != retired_generation) {
            free_.push_back(index_of(current));
        }
        --live_;
    }
    return base::Status::success();
}

base::Status Context::reparent(ElementId element, ElementId parent,
                               std::optional<std::size_t> index) {
    Node* node = find(element);
    if (node == nullptr || find(parent) == nullptr) {
        return invalid_element();
    }
    if (element == primary_root_ || std::ranges::any_of(surface_roots_, [element](const auto& entry) {
            return entry.root == element;
        })) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "the GUI root element cannot be moved");
    }
    for (ElementId ancestor = parent; ancestor.valid();
         ancestor = find(ancestor)->parent) {
        if (ancestor == element) {
            return base::Status::failure(
                base::StatusCode::invalid_argument,
                "a GUI element cannot become its own descendant");
        }
    }
    const ElementId old_parent = node->parent;
    const std::size_t limit =
        find(parent)->children.size() - (old_parent == parent ? 1U : 0U);
    const std::size_t position = index.value_or(limit);
    if (position > limit) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI child index is out of range");
    }
    detach(element, *node);
    auto& siblings = find(parent)->children;
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(position),
                    element);
    node->parent = parent;
    if (find(parent)->placed_children || node->placement || node->popup || !node->visible) {
        refresh_flow(parent);
    }
    mark(old_parent, Dirty::measure);
    mark(parent, Dirty::measure);
    return set_interaction(element, node->interaction);
}

base::Result<ElementId> Context::parent(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    return node->parent;
}

std::span<const ElementId> Context::children(ElementId element) const noexcept {
    const Node* node = find(element);
    if (node == nullptr) {
        return {};
    }
    return node->children;
}

base::Status Context::invalidate(ElementId element, Dirty flags) {
    if (find(element) == nullptr) {
        return invalid_element();
    }
    mark(element, flags);
    return base::Status::success();
}

Dirty Context::dirty(ElementId element) const noexcept {
    const Node* node = find(element);
    return node == nullptr ? Dirty::none : node->dirty;
}

bool Context::has_dirty() const noexcept {
    return std::any_of(dirty_queue_.begin(), dirty_queue_.end(),
                       [this](ElementId element) { return any(dirty(element)); });
}

std::vector<ElementId> Context::dirty_elements() const {
    std::vector<ElementId> result;
    for (const ElementId element : dirty_queue_) {
        if (any(dirty(element))) {
            result.push_back(element);
        }
    }
    return result;
}

void Context::clear_dirty(Dirty flags) noexcept {
    const auto keep = static_cast<Dirty>(~static_cast<std::uint8_t>(flags));
    const auto settle = [&](ElementId element) {
        Node* node = find(element);
        if (node == nullptr) {
            return true;
        }
        node->dirty = node->dirty & keep;
        node->queued = any(node->dirty);
        return !node->queued;
    };
    auto end = std::remove_if(dirty_queue_.begin(), dirty_queue_.end(), settle);
    dirty_queue_.erase(end, dirty_queue_.end());
}

Context::Node* Context::find(ElementId element) noexcept {
    return const_cast<Node*>(std::as_const(*this).find(element));
}

const Context::Node* Context::find(ElementId element) const noexcept {
    if (!element.valid()) {
        return nullptr;
    }
    const std::uint32_t index = index_of(element);
    if (index >= nodes_.size()) {
        return nullptr;
    }
    const Node& node = nodes_[index];
    return node.alive && node.generation == generation_of(element) ? &node : nullptr;
}

ElementId Context::allocate() {
    std::uint32_t index = 0;
    if (free_.empty()) {
        index = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
    } else {
        index = free_.back();
        free_.pop_back();
    }
    Node& node = nodes_[index];
    node.alive = true;
    ++live_;
    return make_id(index, node.generation);
}

void Context::mark(ElementId element, Dirty flags) {
    if (has(flags, Dirty::measure)) {
        flags |= Dirty::arrange;
    }
    if (has(flags, Dirty::arrange)) {
        flags |= Dirty::shape;
    }
    Node* node = find(element);
    if (node == nullptr || !any(flags)) {
        return;
    }
    raise(element, *node, flags);
    if (!has(flags, Dirty::measure)) {
        return;
    }
    // A measured ancestor already implies measured ancestors above it.
    for (ElementId ancestor = node->parent; ancestor.valid();) {
        Node* ancestor_node = find(ancestor);
        if (ancestor_node == nullptr) { break; }
        Node& above = *ancestor_node;
        if (has(above.dirty, Dirty::measure)) {
            break;
        }
        raise(ancestor, above, Dirty::measure | Dirty::arrange);
        ancestor = above.parent;
    }
}

void Context::raise(ElementId element, Node& node, Dirty flags) {
    node.dirty |= flags;
    if (!node.queued) {
        node.queued = true;
        dirty_queue_.push_back(element);
    }
}

base::Status Context::set_layout(ElementId element, std::shared_ptr<const Layout> layout) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    node->layout = std::move(layout);
    mark(element, Dirty::measure);
    return base::Status::success();
}

void Context::refresh_effective_params(Node& node) {
    node.effective_params = node.params;
    const auto& padding = node.styles[static_cast<std::size_t>(Property::padding)];
    if (node.style_padding && (padding.override_value || node.params.padding == Edges{})) {
        node.effective_params.padding = *node.style_padding;
    }
    if (!node.params.width && node.params.min.width == 0) {
        node.effective_params.min.width =
            std::min(node.style_min.width, std::max(0.0F, node.params.max.width));
    }
    if (!node.params.height && node.params.min.height == 0) {
        node.effective_params.min.height =
            std::min(node.style_min.height, std::max(0.0F, node.params.max.height));
    }
}

base::Status Context::set_layout_params(ElementId element, const LayoutParams& params) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    LayoutParams authored = params;
    if (params.padding == node->effective_params.padding) {
        authored.padding = node->params.padding;
    }
    if (node->params == authored) {
        return base::Status::success();
    }
    LayoutParams placement_only = node->params;
    placement_only.position = authored.position;
    placement_only.weight = authored.weight;
    node->params = authored;
    refresh_effective_params(*node);
    if (placement_only == authored) {
        // Only the parent's placement changes; no size is measured again.
        if (Node* parent = find(node->parent)) {
            raise(node->parent, *parent, Dirty::arrange);
        }
    } else {
        mark(element, Dirty::measure);
    }
    return base::Status::success();
}

base::Result<LayoutParams> Context::layout_params(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    LayoutParams params = node->params;
    params.padding = node->effective_params.padding;
    return params;
}

LayoutStats Context::update_layout(Size viewport) {
    stats_ = {};
    if (viewport != viewport_ || !find(root_)->measured) {
        viewport_ = viewport;
        mark(root_, Dirty::measure);
    }
    const bool pending_work =
        std::any_of(dirty_queue_.begin(), dirty_queue_.end(), [this](ElementId element) {
            return has(dirty(element), Dirty::measure | Dirty::arrange);
        });
    if (!pending_work && popups_.empty()) {
        return stats_;
    }

    static_cast<void>(measure_element(root_, Constraints::tight(viewport_)));
    for (const auto& [root, size, surface] : surface_roots_) {
        (void)surface;
        static_cast<void>(measure_element(root, Constraints::tight(size)));
    }

    const auto tree_root = [this](ElementId element) {
        while (const Node* node = find(element)) {
            if (!node->parent.valid()) { return element; }
            element = node->parent;
        }
        return ElementId{};
    };
    const auto popup_viewport = [&](ElementId element) {
        const ElementId root = tree_root(element);
        for (const auto& entry : surface_roots_) {
            if (entry.root == root) { return entry.size; }
        }
        return root == root_ ? viewport_ : find(primary_root_)->rect.size();
    };
    std::vector<std::pair<std::size_t, ElementId>> popups;
    for (const ElementId element : popups_) {
        if (!visible(element)) { continue; }
        std::size_t depth = 0;
        for (auto up = find(element)->parent; up.valid(); up = find(up)->parent) { ++depth; }
        popups.emplace_back(depth, element);
        static_cast<void>(measure_element(element, {{}, popup_viewport(element)}));
    }
    std::stable_sort(popups.begin(), popups.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });

    // Arrange invalidated elements top-down; arranging a parent may already
    // have consumed a descendant's work.
    std::vector<std::pair<std::size_t, ElementId>> pending;
    for (const ElementId element : dirty_queue_) {
        const Node* node = find(element);
        if (node == nullptr || !visible(element) || !has(node->dirty, Dirty::arrange)) {
            continue;
        }
        std::size_t depth = 0;
        bool in_popup = node->popup.has_value();
        for (ElementId up = node->parent; up.valid(); up = find(up)->parent) {
            ++depth;
            in_popup = in_popup || find(up)->popup.has_value();
        }
        if (in_popup) { continue; }
        pending.emplace_back(depth, element);
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    for (const auto& [depth, element] : pending) {
        Node& node = *find(element);
        if (!has(node.dirty, Dirty::arrange)) {
            continue;
        }
        Rect rect = element == root_
                        ? Rect{0.0F, 0.0F, viewport_.width, viewport_.height}
                        : node.rect;
        for (const auto& [root, size, surface] : surface_roots_) {
            (void)surface;
            if (element == root) { rect = {0, 0, size.width, size.height}; break; }
        }
        arrange_element(element, rect);
    }
    for (const auto& [depth, element] : popups) {
        static_cast<void>(depth);
        Node& node = *find(element);
        const Rect anchor = absolute_bounds(node.parent).value();
        const Point offset = child_offset(node.parent);
        const Size size = node.desired;
        const Size view = popup_viewport(element);
        float x = anchor.x;
        float y = anchor.y + anchor.height;
        if (*node.popup == PopupSide::right) {
            x = anchor.x + anchor.width;
            y = anchor.y;
            if (x + size.width > view.width && anchor.x > view.width - x) {
                x = anchor.x - size.width;
            }
        } else if (y + size.height > view.height && anchor.y > view.height - y) {
            y = anchor.y - size.height;
        }
        x = std::clamp(x, 0.0F, std::max(0.0F, view.width - size.width));
        y = std::clamp(y, 0.0F, std::max(0.0F, view.height - size.height));
        arrange_element(element, {x - anchor.x - offset.x, y - anchor.y - offset.y,
                                  size.width, size.height});
    }
    clear_dirty(Dirty::measure | Dirty::arrange);
    return stats_;
}

base::Result<Rect> Context::bounds(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    return node->rect;
}

base::Result<Rect> Context::absolute_bounds(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    Rect rect = node->rect;
    for (ElementId up = node->parent; up.valid();) {
        const Node& above = *find(up);
        rect.x += above.rect.x;
        rect.y += above.rect.y;
        rect.x += above.child_offset.x;
        rect.y += above.child_offset.y;
        up = above.parent;
    }
    return rect;
}

base::Status Context::set_decoration(ElementId element, const Decoration& decoration) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (node->decoration != decoration) {
        node->decoration = decoration;
        raise(element, *node, Dirty::shape);
    }
    return base::Status::success();
}

base::Result<Decoration> Context::decoration(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    return node->decoration;
}

base::Status Context::set_clips_children(ElementId element, bool clips) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (node->clips != clips) {
        node->clips = clips;
        frame_requested_ = true;
    }
    return base::Status::success();
}

bool Context::clips_children(ElementId element) const noexcept {
    const Node* node = find(element);
    return node != nullptr && node->clips;
}

base::Status Context::set_shape_builder(ElementId element,
                                        std::shared_ptr<const ShapeBuilder> builder) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    node->builder = std::move(builder);
    raise(element, *node, Dirty::shape);
    return base::Status::success();
}

base::Status Context::set_text(ElementId element, TextLayout layout, TextureId atlas,
                               Color color) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    const bool has_bitmap_glyph = std::any_of(
        layout.glyphs.begin(), layout.glyphs.end(), [](const TextGlyph& glyph) {
            return glyph.size.width > 0.0F && glyph.size.height > 0.0F;
        });
    if (has_bitmap_glyph && !atlas.valid()) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "visible GUI text requires a registered glyph atlas");
    }
    TextBlock text{std::move(layout), atlas, color};
    node->text_source.reset();
    node->text_color = color;
    if (node->text != text) {
        const bool measure_changed =
            !node->text || node->text->layout != text.layout;
        node->text = std::move(text);
        mark(element, measure_changed ? Dirty::measure : Dirty::shape);
    }
    return base::Status::success();
}

base::Status Context::clear_text(ElementId element) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    node->text_source.reset();
    if (node->text) {
        node->text.reset();
        mark(element, Dirty::measure);
    }
    return base::Status::success();
}

ShapeStats Context::update_shapes() {
    static const DecorationBuilder fallback;
    ShapeStats stats;
    for (const ElementId element : dirty_queue_) {
        Node* node = find(element);
        if (node == nullptr || !has(node->dirty, Dirty::shape)) {
            continue;
        }
        node->shapes.vertices.clear();
        node->shapes.indices.clear();
        node->shapes.segments.clear();
        ShapeWriter writer(node->shapes);
        const ShapeBuilder& builder = node->builder ? *node->builder : fallback;
        builder.build(writer, node->rect.size(), node->decoration);
        if (node->text) {
            const TextBlock& text = *node->text;
            const Decoration glyph_decoration{.fill = text.color, .texture = text.atlas};
            const Edges padding = node->effective_params.padding;
            for (const TextGlyph& glyph : text.layout.glyphs) {
                if (glyph.size.width <= 0.0F || glyph.size.height <= 0.0F) {
                    continue;
                }
                Decoration decoration = glyph_decoration;
                decoration.uv = glyph.uv;
                writer.rect({padding.left + glyph.position.x, padding.top + glyph.position.y,
                             glyph.size.width,
                             glyph.size.height},
                            decoration);
            }
        }
        ++stats.regenerated;
    }
    clear_dirty(Dirty::shape);
    return stats;
}

const ElementShapes* Context::shapes(ElementId element) const noexcept {
    const Node* node = find(element);
    return node == nullptr ? nullptr : &node->shapes;
}

base::Status Context::set_viewport(Size size, float scale) {
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 0.0F ||
        size.height < 0.0F || !std::isfinite(scale) || scale <= 0.0F) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI viewport must be finite and non-negative, "
                                     "and its scale positive");
    }
    if (size != host_viewport_ || scale != scale_) {
        host_viewport_ = size;
        scale_ = scale;
        frame_requested_ = true;
    }
    return base::Status::success();
}

Size Context::viewport() const noexcept {
    return host_viewport_;
}

float Context::scale() const noexcept {
    return scale_;
}

base::Status Context::handle(const InputEvent& event, Clock::time_point now) {
    time_ = now;
    event_status_ = base::Status::success();
    std::visit(
        [this](const auto& value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, PointerEvent>) {
                input_.modifiers = value.modifiers;
                switch (value.action) {
                case PointerAction::move:
                    input_.pointer = value.position;
                    break;
                case PointerAction::down:
                    input_.pointer = value.position;
                    input_.buttons |= button_bit(value.button);
                    break;
                case PointerAction::up:
                    input_.pointer = value.position;
                    input_.buttons &= static_cast<std::uint8_t>(~button_bit(value.button));
                    break;
                case PointerAction::leave:
                    input_.pointer.reset();
                    break;
                case PointerAction::cancel:
                    input_.pointer.reset();
                    input_.buttons = 0;
                    break;
                }
            } else if constexpr (std::is_same_v<Event, WheelEvent>) {
                input_.pointer = value.position;
                input_.modifiers = value.modifiers;
            } else if constexpr (std::is_same_v<Event, KeyEvent>) {
                input_.modifiers = value.modifiers;
            } else if constexpr (std::is_same_v<Event, CompositionEvent>) {
                const std::size_t size = value.preedit.size();
                input_.composition.preedit = value.preedit;
                input_.composition.selection_begin = std::min(value.selection_begin, size);
                input_.composition.selection_end =
                    std::clamp(value.selection_end, input_.composition.selection_begin, size);
            } else if constexpr (std::is_same_v<Event, FocusEvent>) {
                input_.focused = value.focused;
                if (!value.focused) {
                    input_.buttons = 0;
                    input_.modifiers = Modifiers::none;
                    input_.composition = {};
                }
            }
        },
        event);
    const auto filtered = filter_input(event);
    if (!filtered.ok()) {
        event_status_ = filtered.status();
        return event_status_;
    }
    route_input(event, filtered.value());
    return event_status_;
}

base::Status Context::set_child_offset(ElementId element, Point offset) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI child offset must be finite");
    }
    if (node->child_offset != offset) {
        node->child_offset = offset;
        request_frame();
    }
    return base::Status::success();
}

Point Context::child_offset(ElementId element) const noexcept {
    const Node* node = find(element);
    return node == nullptr ? Point{} : node->child_offset;
}

base::Status Context::set_semantics(ElementId element, Semantics semantics) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    node->semantics = std::move(semantics);
    return base::Status::success();
}

base::Result<Context::Semantics> Context::semantics(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    return node->semantics;
}

const InputState& Context::input() const noexcept {
    return input_;
}

void Context::request_frame() noexcept {
    frame_requested_ = true;
}

void Context::request_frame_at(Clock::time_point when) {
    deadlines_.push_back(when);
}

bool Context::needs_frame(Clock::time_point now) const noexcept {
    if (frame_requested_ || has_dirty() || updates_due()) {
        return true;
    }
    const auto next = next_update();
    return next.has_value() && *next <= now;
}

std::optional<Context::Clock::time_point> Context::next_update() const noexcept {
    std::optional<Clock::time_point> next;
    for (const auto deadline : deadlines_) {
        if (!next || deadline < *next) { next = deadline; }
    }
    for (const auto& entry : updaters_) {
        if (entry.deadline && (!next || *entry.deadline < *next)) { next = entry.deadline; }
    }
    return next;
}

FrameStats Context::prepare_frame(Clock::time_point now) {
    time_ = now;
    FrameStats stats;
    stats.status = run_updaters(stats.updates);
    if (!stats.status.ok()) {
        return stats;
    }
    auto styles = update_styles();
    if (!styles.ok()) {
        stats.status = styles.status();
        return stats;
    }
    stats.styles = styles.value();
    stats.layout = update_layout(host_viewport_);
    if (std::exchange(layout_requested_update_, false)) {
        // Size-dependent content asked for one more pass (LayoutPass::request_update).
        stats.status = run_updaters(stats.updates);
        if (!stats.status.ok()) {
            return stats;
        }
        styles = update_styles();
        if (!styles.ok()) {
            stats.status = styles.status();
            return stats;
        }
        stats.styles.resolved += styles.value().resolved;
        const auto again = update_layout(host_viewport_);
        stats.layout.measured += again.measured;
        stats.layout.arranged += again.arranged;
    }
    stats.shapes = update_shapes();
    // Style handlers can enqueue new style work after the current snapshot.
    clear_dirty(Dirty::measure | Dirty::arrange | Dirty::shape);
    frame_requested_ = false;
    std::erase_if(deadlines_, [now](Clock::time_point when) { return when <= now; });
    return stats;
}

void Context::set_host(Host* host) noexcept {
    host_ = host;
}

Host& Context::host() const noexcept {
    static Host idle;
    return host_ == nullptr ? idle : *host_;
}

const LayoutParams& Context::params_of(ElementId element) const noexcept {
    static const LayoutParams defaults;
    const Node* node = find(element);
    return node == nullptr ? defaults : node->effective_params;
}

Size Context::desired_of(ElementId element) const noexcept {
    const Node* node = find(element);
    return node == nullptr ? Size{} : node->desired;
}

Size Context::measure_element(ElementId element, Constraints constraints) {
    Node* node = find(element);
    if (node == nullptr) {
        return {};
    }
    if (node->measured && !has(node->dirty, Dirty::measure) &&
        node->measured_under == constraints) {
        return node->desired;
    }
    ++stats_.measured;

    const LayoutParams params = node->effective_params;
    const auto range = [](float low, float high, float own_min, float own_max,
                          const std::optional<float>& fixed) {
        const float lo = std::max(low, std::min(own_min, high));
        const float hi = std::max(lo, std::min(high, own_max));
        return fixed ? std::pair{std::clamp(*fixed, lo, hi), std::clamp(*fixed, lo, hi)}
                     : std::pair{lo, hi};
    };
    const auto [min_w, max_w] = range(constraints.min.width, constraints.max.width,
                                      params.min.width, params.max.width, params.width);
    const auto [min_h, max_h] = range(constraints.min.height, constraints.max.height,
                                      params.min.height, params.max.height, params.height);
    const Constraints content{
        {},
        {std::max(0.0F, max_w - params.padding.horizontal()),
         std::max(0.0F, max_h - params.padding.vertical())}};

    static const FreeLayout fallback;
    const std::shared_ptr<const Layout> layout = node->layout;
    LayoutPass pass(*this);
    const Size inner = (layout ? *layout : static_cast<const Layout&>(fallback))
                           .measure(pass, element, content);

    node = find(element);
    if (node->placed_children) {
        const std::vector<ElementId> children = node->children;
        for (const ElementId child : children) {
            if (const auto placed = find(child)->placement; placed && visible(child)) {
                static_cast<void>(measure_element(child, Constraints::tight(placed->size())));
            }
        }
        node = find(element);
    }
    const Size text_size = node->text ? node->text->layout.size : Size{};
    node->desired = {
        std::clamp(std::max(inner.width, text_size.width) + params.padding.horizontal(),
                   min_w, max_w),
        std::clamp(std::max(inner.height, text_size.height) + params.padding.vertical(),
                   min_h, max_h)};
    node->measured_under = constraints;
    node->measured = true;
    node->dirty = node->dirty & without(Dirty::measure);
    return node->desired;
}

void Context::arrange_element(ElementId element, Rect rect) {
    Node* node = find(element);
    if (node == nullptr) {
        return;
    }
    const bool resized = !node->arranged || node->rect.size() != rect.size();
    node->rect = rect;
    if (resized) {
        raise(element, *node, Dirty::shape);
    } else if (!has(node->dirty, Dirty::arrange)) {
        return;
    }
    ++stats_.arranged;
    node->arranged = true;
    node->dirty = node->dirty & without(Dirty::arrange);

    const Edges padding = node->effective_params.padding;
    const Rect content{padding.left, padding.top,
                       std::max(0.0F, rect.width - padding.horizontal()),
                       std::max(0.0F, rect.height - padding.vertical())};
    static const FreeLayout fallback;
    const std::shared_ptr<const Layout> layout = node->layout;
    LayoutPass pass(*this);
    (layout ? *layout : static_cast<const Layout&>(fallback)).arrange(pass, element, content);

    node = find(element);
    if (node->placed_children) {
        const std::vector<ElementId> children = node->children;
        for (const ElementId child : children) {
            if (const auto& placed = find(child)->placement; placed && visible(child)) {
                arrange_element(child, *placed);
            }
        }
    }
}

void Context::detach(ElementId element, Node& node) {
    if (Node* parent = find(node.parent)) {
        auto& siblings = parent->children;
        siblings.erase(std::find(siblings.begin(), siblings.end(), element));
        if (parent->placed_children) {
            refresh_flow(node.parent);
        }
    }
    node.parent = {};
}

Context::Clock::time_point Context::time() const noexcept { return time_; }

} // namespace truffle::gui

#include "truffle/gui/context.hpp"

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

Context::Context() : root_(allocate()) {
    mark(root_, all_dirty);
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
    mark(element, all_dirty);
    return element;
}

base::Status Context::destroy(ElementId element) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (element == root_) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "the GUI root element cannot be destroyed");
    }
    const ElementId parent = node->parent;
    detach(element, *node);
    mark(parent, Dirty::measure);

    std::vector<ElementId> pending{element};
    while (!pending.empty()) {
        const ElementId current = pending.back();
        pending.pop_back();
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
    if (element == root_) {
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
    mark(old_parent, Dirty::measure);
    mark(parent, Dirty::measure);
    return base::Status::success();
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
        Node& above = *find(ancestor);
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

base::Status Context::set_layout_params(ElementId element, const LayoutParams& params) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (node->params == params) {
        return base::Status::success();
    }
    LayoutParams placement_only = node->params;
    placement_only.position = params.position;
    placement_only.weight = params.weight;
    node->params = params;
    if (placement_only == params) {
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
    return node->params;
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
    if (!pending_work) {
        return stats_;
    }

    static_cast<void>(measure_element(root_, Constraints::tight(viewport_)));

    // Arrange invalidated elements top-down; arranging a parent may already
    // have consumed a descendant's work.
    std::vector<std::pair<std::size_t, ElementId>> pending;
    for (const ElementId element : dirty_queue_) {
        const Node* node = find(element);
        if (node == nullptr || !has(node->dirty, Dirty::arrange)) {
            continue;
        }
        std::size_t depth = 0;
        for (ElementId up = node->parent; up.valid(); up = find(up)->parent) {
            ++depth;
        }
        pending.emplace_back(depth, element);
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    for (const auto& [depth, element] : pending) {
        Node& node = *find(element);
        if (!has(node.dirty, Dirty::arrange)) {
            continue;
        }
        const Rect rect = element == root_
                              ? Rect{0.0F, 0.0F, viewport_.width, viewport_.height}
                              : node.rect;
        arrange_element(element, rect);
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

void Context::handle(const InputEvent& event) {
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
    if (frame_requested_ || has_dirty()) {
        return true;
    }
    const auto next = next_update();
    return next.has_value() && *next <= now;
}

std::optional<Context::Clock::time_point> Context::next_update() const noexcept {
    if (deadlines_.empty()) {
        return std::nullopt;
    }
    return *std::min_element(deadlines_.begin(), deadlines_.end());
}

FrameStats Context::prepare_frame(Clock::time_point now) {
    FrameStats stats;
    stats.layout = update_layout(host_viewport_);
    stats.shapes = update_shapes();
    clear_dirty();
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
    return node == nullptr ? defaults : node->params;
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

    const LayoutParams params = node->params;
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
    node->desired = {std::clamp(inner.width + params.padding.horizontal(), min_w, max_w),
                     std::clamp(inner.height + params.padding.vertical(), min_h, max_h)};
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

    const Edges padding = node->params.padding;
    const Rect content{padding.left, padding.top,
                       std::max(0.0F, rect.width - padding.horizontal()),
                       std::max(0.0F, rect.height - padding.vertical())};
    static const FreeLayout fallback;
    const std::shared_ptr<const Layout> layout = node->layout;
    LayoutPass pass(*this);
    (layout ? *layout : static_cast<const Layout&>(fallback)).arrange(pass, element, content);
}

void Context::detach(ElementId element, Node& node) {
    if (Node* parent = find(node.parent)) {
        auto& siblings = parent->children;
        siblings.erase(std::find(siblings.begin(), siblings.end(), element));
    }
    node.parent = {};
}

} // namespace truffle::gui

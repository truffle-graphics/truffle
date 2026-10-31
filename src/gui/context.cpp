#include "truffle/gui/context.hpp"

#include <algorithm>
#include <limits>
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
        doomed.children.clear();
        doomed.alive = false;
        doomed.queued = false;
        doomed.dirty = Dirty::none;
        doomed.parent = {};
        ++doomed.generation;
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
    node->dirty |= flags;
    if (!node->queued) {
        node->queued = true;
        dirty_queue_.push_back(element);
    }
    if (!has(flags, Dirty::measure)) {
        return;
    }
    // A measured ancestor already implies measured ancestors above it.
    for (ElementId ancestor = node->parent; ancestor.valid();) {
        Node& above = *find(ancestor);
        if (has(above.dirty, Dirty::measure)) {
            break;
        }
        above.dirty |= Dirty::measure | Dirty::arrange;
        if (!above.queued) {
            above.queued = true;
            dirty_queue_.push_back(ancestor);
        }
        ancestor = above.parent;
    }
}

void Context::detach(ElementId element, Node& node) {
    if (Node* parent = find(node.parent)) {
        auto& siblings = parent->children;
        siblings.erase(std::find(siblings.begin(), siblings.end(), element));
    }
    node.parent = {};
}

} // namespace truffle::gui

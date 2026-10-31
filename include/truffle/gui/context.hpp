#pragma once

#include "truffle/base/status.hpp"
#include "truffle/gui/element.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace truffle::gui {

// Owns a persistent tree of elements. A context always has a root element.
// It is not thread-safe; use it from one thread at a time.
//
// Invalidation rules:
// - `measure` on an element also marks its own `arrange` and `shape`, and marks
//   `measure` and `arrange` on every ancestor, because their sizes and the
//   placement of siblings may depend on it.
// - `arrange` marks the element's own `arrange` and `shape`.
// - `style` and `shape` mark only the element itself.
// - Adding, removing or moving a child marks the affected parents with
//   `measure`; a newly created element starts with every flag set.
class Context {
public:
    Context();

    [[nodiscard]] ElementId root() const noexcept;
    [[nodiscard]] bool alive(ElementId element) const noexcept;
    [[nodiscard]] std::size_t element_count() const noexcept;

    // Appends a new element as the last child of `parent`.
    [[nodiscard]] base::Result<ElementId> create(ElementId parent);

    // Destroys `element` and its whole subtree. The root cannot be destroyed.
    [[nodiscard]] base::Status destroy(ElementId element);

    // Moves `element` under `parent` at `index` (append when empty). Moving
    // within the same parent reorders it. Cycles and the root are rejected.
    [[nodiscard]] base::Status reparent(ElementId element, ElementId parent,
                                        std::optional<std::size_t> index = std::nullopt);

    [[nodiscard]] base::Result<ElementId> parent(ElementId element) const;

    // Children in drawing order; empty for invalid handles.
    [[nodiscard]] std::span<const ElementId> children(ElementId element) const noexcept;

    [[nodiscard]] base::Status invalidate(ElementId element, Dirty flags);

    // Dirty flags of `element`; `Dirty::none` for invalid handles.
    [[nodiscard]] Dirty dirty(ElementId element) const noexcept;
    [[nodiscard]] bool has_dirty() const noexcept;

    // Live dirty elements in the order they first became dirty.
    [[nodiscard]] std::vector<ElementId> dirty_elements() const;

    // Clears the given flags on every element. Later update stages call this
    // after consuming their work.
    void clear_dirty(Dirty flags = Dirty::style | Dirty::measure | Dirty::arrange |
                                   Dirty::shape) noexcept;

private:
    struct Node {
        std::uint32_t generation = 1;
        bool alive = false;
        bool queued = false;
        Dirty dirty = Dirty::none;
        ElementId parent;
        std::vector<ElementId> children;
    };

    [[nodiscard]] Node* find(ElementId element) noexcept;
    [[nodiscard]] const Node* find(ElementId element) const noexcept;
    [[nodiscard]] ElementId allocate();
    void mark(ElementId element, Dirty flags);
    void detach(ElementId element, Node& node);

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_;
    std::vector<ElementId> dirty_queue_;
    std::size_t live_ = 0;
    ElementId root_;
};

} // namespace truffle::gui

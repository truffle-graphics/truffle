#pragma once

#include "truffle/base/status.hpp"
#include "truffle/gui/element.hpp"
#include "truffle/gui/geometry.hpp"
#include "truffle/gui/layout.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
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
//
// Layout rules:
// - `update_layout` consumes `measure` and `arrange` work only; an unchanged
//   tree does no layout work.
// - Element bounds are relative to the parent, so moving an element does not
//   touch its subtree. An element whose size changes is marked `shape`.
// - Changing only `position` or `weight` re-arranges the parent without
//   measuring anything.
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

    // Sets how `element` places its children; `nullptr` restores FreeLayout.
    [[nodiscard]] base::Status set_layout(ElementId element,
                                          std::shared_ptr<const Layout> layout);
    [[nodiscard]] base::Status set_layout_params(ElementId element,
                                                 const LayoutParams& params);
    [[nodiscard]] base::Result<LayoutParams> layout_params(ElementId element) const;

    // Lays out invalidated parts of the tree; the root fills `viewport`.
    LayoutStats update_layout(Size viewport);

    // Bounds from the last `update_layout`, relative to the parent's origin.
    [[nodiscard]] base::Result<Rect> bounds(ElementId element) const;
    // Bounds relative to the root's origin.
    [[nodiscard]] base::Result<Rect> absolute_bounds(ElementId element) const;

private:
    friend class LayoutPass;

    struct Node {
        std::uint32_t generation = 1;
        bool alive = false;
        bool queued = false;
        bool measured = false;
        bool arranged = false;
        Dirty dirty = Dirty::none;
        ElementId parent;
        std::vector<ElementId> children;
        std::shared_ptr<const Layout> layout;
        LayoutParams params;
        Constraints measured_under;
        Size desired;
        Rect rect;
    };

    [[nodiscard]] Node* find(ElementId element) noexcept;
    [[nodiscard]] const Node* find(ElementId element) const noexcept;
    [[nodiscard]] ElementId allocate();
    void mark(ElementId element, Dirty flags);
    // Sets exactly `flags` on one element, without implied flags or ancestors.
    void raise(ElementId element, Node& node, Dirty flags);
    void detach(ElementId element, Node& node);

    [[nodiscard]] const LayoutParams& params_of(ElementId element) const noexcept;
    [[nodiscard]] Size desired_of(ElementId element) const noexcept;
    Size measure_element(ElementId element, Constraints constraints);
    void arrange_element(ElementId element, Rect rect);

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_;
    std::vector<ElementId> dirty_queue_;
    std::size_t live_ = 0;
    ElementId root_;
    Size viewport_;
    LayoutStats stats_;
};

} // namespace truffle::gui

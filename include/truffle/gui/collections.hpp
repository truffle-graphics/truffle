#pragma once

#include "truffle/gui/context.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace truffle::gui {

struct VirtualListDesc {
    std::size_t count = 0;
    float row_height = 24.0F;
    // Builds a pooled row element once (children, styling, layout).
    std::function<base::Status(Context&, ElementId row)> make_row;
    // Shows item `index` in a pooled row; called when a row is given a new
    // index or refreshed. Must not destroy the row.
    std::function<base::Status(Context&, ElementId row, std::size_t index)> bind_row;
    // Primary press on an item. Selection belongs to the application.
    std::function<void(std::size_t index)> activated;
};

// A vertical list of `count` fixed-height items where only the visible rows
// have live elements: a pool sized to the arranged height is recycled as the
// list scrolls (wheel, arrow/page/home/end keys or `set_scroll`). Give it a
// bounded height (fixed, weight or parent constraint). The list element clips
// its rows. A non-owning handle; the element and its rows belong to Context.
class VirtualList {
public:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    VirtualList() = default;
    [[nodiscard]] static base::Result<VirtualList> create(Context& context, ElementId parent,
                                                          VirtualListDesc desc);
    [[nodiscard]] ElementId element() const noexcept;
    [[nodiscard]] bool alive() const noexcept;

    // Changing the count or refreshing rebinds rows at the next frame.
    [[nodiscard]] base::Status set_count(std::size_t count);
    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] base::Status refresh();
    [[nodiscard]] base::Status refresh(std::size_t index);

    // Vertical offset in pixels, clamped to the content at the next frame.
    [[nodiscard]] base::Status set_scroll(double offset);
    [[nodiscard]] double scroll() const noexcept;
    // Scrolls the least distance that shows `index` fully.
    [[nodiscard]] base::Status scroll_to(std::size_t index);

    // Items with live rows after the latest frame, as [first, last).
    [[nodiscard]] std::size_t first_visible() const noexcept;
    [[nodiscard]] std::size_t last_visible() const noexcept;
    // Row element showing `index`, or an empty id when it has no live row.
    [[nodiscard]] ElementId row(std::size_t index) const noexcept;

private:
    struct State;
    explicit VirtualList(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

// One shared layout for table rows and their header: a single-row grid with
// the given columns. Cells pick a column with `LayoutParams::cell`.
[[nodiscard]] std::shared_ptr<const Layout> table_columns(std::vector<Track> columns,
                                                          float spacing = 0.0F);

// A visible tree node in depth-first order, for a VirtualList over a tree.
struct TreeRow {
    std::uint64_t node = 0;
    std::uint32_t depth = 0;
    bool has_children = false;
};

struct TreeSource {
    // Appends the children of `node` to `out`.
    std::function<void(std::uint64_t node, std::vector<std::uint64_t>& out)> children;
    // Expansion state, owned by the application.
    std::function<bool(std::uint64_t node)> expanded;
};

// Flattens the visible part of a tree; collapsed subtrees are not visited.
[[nodiscard]] std::vector<TreeRow> flatten_tree(std::span<const std::uint64_t> roots,
                                                const TreeSource& source);

} // namespace truffle::gui

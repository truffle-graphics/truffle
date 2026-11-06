#include "test_support.hpp"
#include "truffle/gui/collections.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace truffle;
using namespace truffle::gui;

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

void set_height(Context& c, ElementId element, float height) {
    auto params = need(c.layout_params(element));
    params.width = 300.0F;
    params.height = height;
    need(c.set_layout_params(element, params));
}

int million_rows() {
    Context c;
    std::size_t binds = 0;
    std::vector<std::size_t> activated;
    auto list = need(VirtualList::create(c, c.root(), {
        .count = 1'000'000,
        .row_height = 20,
        .bind_row = [&](Context&, ElementId, std::size_t) { ++binds; return base::Status::success(); },
        .activated = [&](std::size_t index) { activated.push_back(index); },
    }));
    set_height(c, list.element(), 200);
    TFL_CHECK(c.set_viewport({400, 400}).ok());
    auto stats = frame(c);
    TFL_CHECK(stats.status.ok());
    // Ten visible rows plus one partially shown while scrolling.
    TFL_CHECK(c.children(list.element()).size() == 11 && c.element_count() == 13);
    TFL_CHECK(binds == 11 && list.first_visible() == 0 && list.last_visible() == 11);
    TFL_CHECK(need(c.bounds(list.row(3))) == (Rect{0, 60, 300, 20}));
    TFL_CHECK(!c.needs_frame(Context::Clock::now()));

    // Scrolling within a row only moves the content: no binds, layout or shapes.
    binds = 0;
    need(list.set_scroll(7));
    stats = frame(c);
    TFL_CHECK(stats.updates == 1 && binds == 0);
    TFL_CHECK(stats.layout.measured == 0 && stats.layout.arranged == 0);
    TFL_CHECK(stats.shapes.regenerated == 0);
    TFL_CHECK(c.child_offset(list.element()) == (Point{0, -7}));

    // One row further recycles exactly one row.
    need(list.set_scroll(27));
    TFL_CHECK(frame(c).status.ok() && binds == 1 && list.first_visible() == 1);
    TFL_CHECK(c.child_offset(list.element()) == (Point{0, -7}));

    // A jump deep into the data keeps the element count bounded.
    binds = 0;
    need(list.set_scroll(500'000.0 * 20));
    TFL_CHECK(frame(c).status.ok());
    TFL_CHECK(list.first_visible() == 500'000 && c.element_count() == 13 && binds == 11);
    TFL_CHECK(need(c.bounds(list.row(500'002))).y == 40);

    // Input: a press activates the item under the pointer, keys and wheel scroll.
    need(c.handle(PointerEvent{.action = PointerAction::down, .position = {10, 45},
                               .button = PointerButton::primary}));
    TFL_CHECK(activated.size() == 1 && activated[0] == 500'002);
    TFL_CHECK(c.focused() == list.element());
    need(c.handle(KeyEvent{.key = Key::end}));
    TFL_CHECK(frame(c).status.ok() && list.last_visible() == 1'000'000);
    TFL_CHECK(list.scroll() == 1'000'000.0 * 20 - 200);
    need(c.handle(WheelEvent{.position = {10, 10}, .delta_y = 1}));
    TFL_CHECK(frame(c).status.ok() && list.scroll() == 1'000'000.0 * 20 - 220);
    need(c.handle(KeyEvent{.key = Key::home}));
    TFL_CHECK(frame(c).status.ok() && list.scroll() == 0 && list.first_visible() == 0);
    need(list.scroll_to(30));
    TFL_CHECK(frame(c).status.ok() && list.scroll() == 31.0 * 20 - 200);
    return 0;
}

int resizing_and_counts() {
    Context c;
    std::size_t made = 0;
    auto list = need(VirtualList::create(c, c.root(), {
        .count = 100,
        .row_height = 10,
        .make_row = [&](Context&, ElementId) { ++made; return base::Status::success(); },
    }));
    set_height(c, list.element(), 50);
    TFL_CHECK(c.set_viewport({400, 400}).ok());
    TFL_CHECK(frame(c).status.ok() && c.children(list.element()).size() == 6 && made == 6);

    // A taller list grows its pool within the same frame.
    set_height(c, list.element(), 100);
    auto stats = frame(c);
    TFL_CHECK(stats.status.ok() && stats.updates == 1);
    TFL_CHECK(c.children(list.element()).size() == 11 && made == 11);
    TFL_CHECK(need(c.bounds(list.row(10))) == (Rect{0, 100, 300, 10}));
    TFL_CHECK(!c.needs_frame(Context::Clock::now()));

    need(list.set_count(3));
    TFL_CHECK(frame(c).status.ok() && c.children(list.element()).size() == 3);
    need(list.set_count(0));
    TFL_CHECK(frame(c).status.ok() && c.children(list.element()).empty());
    TFL_CHECK(list.first_visible() == list.last_visible());
    TFL_CHECK(!VirtualList::create(c, c.root(), {.row_height = 0}).ok());
    TFL_CHECK(c.destroy(list.element()).ok() && !list.alive() && !list.refresh().ok());
    return 0;
}

int tables_and_trees() {
    Context c;
    const auto columns = table_columns({Track::fixed(100), Track::weighted(1)}, 4);
    std::vector<std::pair<std::size_t, ElementId>> bound;
    auto list = need(VirtualList::create(c, c.root(), {
        .count = 50,
        .row_height = 16,
        .make_row = [&](Context& ctx, ElementId row) {
            need(ctx.set_layout(row, columns));
            for (std::uint16_t column = 0; column < 2; ++column) {
                const auto cell = need(ctx.create(row));
                LayoutParams params;
                params.cell.column = column;
                need(ctx.set_layout_params(cell, params));
            }
            return base::Status::success();
        },
        .bind_row = [&](Context&, ElementId row, std::size_t index) {
            bound.emplace_back(index, row);
            return base::Status::success();
        },
    }));
    set_height(c, list.element(), 32);
    TFL_CHECK(c.set_viewport({400, 400}).ok());
    TFL_CHECK(frame(c).status.ok());
    const auto cells = c.children(list.row(1));
    TFL_CHECK(cells.size() == 2);
    TFL_CHECK(need(c.bounds(cells[1])) == (Rect{104, 0, 196, 16}));
    TFL_CHECK(bound.size() == 3 && c.element_count() == 2 + 3 * 3);

    // A tree: 0 -> {1 -> {3}, 2}; only expanded nodes are visited.
    const auto source = TreeSource{
        .children = [](std::uint64_t node, std::vector<std::uint64_t>& out) {
            if (node == 0) { out = {1, 2}; }
            if (node == 1) { out = {3}; }
        },
        .expanded = [](std::uint64_t node) { return node == 0; },
    };
    const std::uint64_t roots[] = {0};
    const auto rows = flatten_tree(roots, source);
    TFL_CHECK(rows.size() == 3);
    TFL_CHECK(rows[0].node == 0 && rows[0].depth == 0 && rows[0].has_children);
    TFL_CHECK(rows[1].node == 1 && rows[1].depth == 1 && rows[1].has_children);
    TFL_CHECK(rows[2].node == 2 && rows[2].depth == 1 && !rows[2].has_children);
    return 0;
}

} // namespace

int main() {
    return million_rows() + resizing_and_counts() + tables_and_trees();
}

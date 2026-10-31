#include "test_support.hpp"
#include "truffle/gui/context.hpp"

#include <memory>
#include <vector>

namespace {

using truffle::gui::Align;
using truffle::gui::Axis;
using truffle::gui::Constraints;
using truffle::gui::Context;
using truffle::gui::Dirty;
using truffle::gui::ElementId;
using truffle::gui::Layout;
using truffle::gui::LayoutParams;
using truffle::gui::LayoutPass;
using truffle::gui::Rect;
using truffle::gui::Size;
using truffle::gui::StackLayout;
using truffle::gui::StackOptions;

ElementId make(Context& context, ElementId parent, LayoutParams params = {}) {
    auto created = context.create(parent);
    if (!created.ok()) {
        return {};
    }
    static_cast<void>(context.set_layout_params(created.value(), params));
    return created.value();
}

LayoutParams fixed(float width, float height) {
    LayoutParams params;
    params.width = width;
    params.height = height;
    return params;
}

Rect at(const Context& context, ElementId element) {
    auto bounds = context.bounds(element);
    return bounds.ok() ? bounds.value() : Rect{-1.0F, -1.0F, -1.0F, -1.0F};
}

std::shared_ptr<const Layout> stack(Axis axis, float spacing = 0.0F,
                                    Align main = Align::start,
                                    Align cross = Align::stretch) {
    return std::make_shared<StackLayout>(StackOptions{axis, spacing, main, cross});
}

int test_column_padding_spacing() {
    Context context;
    const ElementId root = context.root();
    LayoutParams padded;
    padded.padding = {10.0F, 10.0F, 10.0F, 10.0F};
    TRUFFLE_CHECK(context.set_layout_params(root, padded).ok());
    TRUFFLE_CHECK(context.set_layout(root, stack(Axis::vertical, 5.0F)).ok());
    LayoutParams first;
    first.height = 20.0F;
    LayoutParams second;
    second.height = 30.0F;
    const ElementId a = make(context, root, first);
    const ElementId b = make(context, root, second);
    context.update_layout({200.0F, 100.0F});
    TRUFFLE_CHECK(at(context, root) == (Rect{0.0F, 0.0F, 200.0F, 100.0F}));
    TRUFFLE_CHECK(at(context, a) == (Rect{10.0F, 10.0F, 180.0F, 20.0F}));
    TRUFFLE_CHECK(at(context, b) == (Rect{10.0F, 35.0F, 180.0F, 30.0F}));

    // Removing a child moves its siblings up.
    TRUFFLE_CHECK(context.destroy(a).ok());
    context.update_layout({200.0F, 100.0F});
    TRUFFLE_CHECK(at(context, b) == (Rect{10.0F, 10.0F, 180.0F, 30.0F}));
    return 0;
}

int test_row_alignment() {
    Context context;
    const ElementId root = context.root();
    TRUFFLE_CHECK(
        context.set_layout(root, stack(Axis::horizontal, 0.0F, Align::end, Align::center)).ok());
    const ElementId a = make(context, root, fixed(20.0F, 10.0F));
    const ElementId b = make(context, root, fixed(30.0F, 20.0F));
    context.update_layout({100.0F, 50.0F});
    TRUFFLE_CHECK(at(context, a) == (Rect{50.0F, 20.0F, 20.0F, 10.0F}));
    TRUFFLE_CHECK(at(context, b) == (Rect{70.0F, 15.0F, 30.0F, 20.0F}));

    TRUFFLE_CHECK(
        context.set_layout(root, stack(Axis::horizontal, 0.0F, Align::center, Align::start)).ok());
    context.update_layout({100.0F, 50.0F});
    TRUFFLE_CHECK(at(context, a) == (Rect{25.0F, 0.0F, 20.0F, 10.0F}));
    TRUFFLE_CHECK(at(context, b) == (Rect{45.0F, 0.0F, 30.0F, 20.0F}));
    return 0;
}

int test_stretching() {
    Context context;
    const ElementId root = context.root();
    TRUFFLE_CHECK(context.set_layout(root, stack(Axis::horizontal)).ok());
    LayoutParams solid;
    solid.width = 50.0F;
    LayoutParams one;
    one.weight = 1.0F;
    LayoutParams two;
    two.weight = 2.0F;
    const ElementId f = make(context, root, solid);
    const ElementId w1 = make(context, root, one);
    const ElementId w2 = make(context, root, two);
    context.update_layout({300.0F, 40.0F});
    // 250 shared 1:2, with edges snapped to whole units and no gaps.
    TRUFFLE_CHECK(at(context, f) == (Rect{0.0F, 0.0F, 50.0F, 40.0F}));
    TRUFFLE_CHECK(at(context, w1) == (Rect{50.0F, 0.0F, 83.0F, 40.0F}));
    TRUFFLE_CHECK(at(context, w2) == (Rect{133.0F, 0.0F, 167.0F, 40.0F}));

    // A maximum freezes the child; the rest goes to the others.
    one.max.width = 60.0F;
    TRUFFLE_CHECK(context.set_layout_params(w1, one).ok());
    context.update_layout({300.0F, 40.0F});
    TRUFFLE_CHECK(at(context, w1) == (Rect{50.0F, 0.0F, 60.0F, 40.0F}));
    TRUFFLE_CHECK(at(context, w2) == (Rect{110.0F, 0.0F, 190.0F, 40.0F}));

    // Shrinking stops at minimum sizes, even if that overflows.
    one = {};
    one.weight = 1.0F;
    one.min.width = 30.0F;
    two.min.width = 40.0F;
    TRUFFLE_CHECK(context.set_layout_params(w1, one).ok());
    TRUFFLE_CHECK(context.set_layout_params(w2, two).ok());
    context.update_layout({100.0F, 40.0F});
    TRUFFLE_CHECK(at(context, w1) == (Rect{50.0F, 0.0F, 30.0F, 40.0F}));
    TRUFFLE_CHECK(at(context, w2) == (Rect{80.0F, 0.0F, 40.0F, 40.0F}));

    // A fixed main size wins over a weight.
    LayoutParams weighted_fixed = fixed(10.0F, 10.0F);
    weighted_fixed.weight = 5.0F;
    TRUFFLE_CHECK(context.set_layout_params(f, weighted_fixed).ok());
    context.update_layout({100.0F, 40.0F});
    TRUFFLE_CHECK(at(context, f).width == 10.0F);
    return 0;
}

int test_min_max_and_free_placement() {
    Context context;
    const ElementId root = context.root();
    LayoutParams panel_params = fixed(40.0F, 30.0F);
    panel_params.position = {15.0F, 25.0F};
    panel_params.padding.left = 2.0F;
    const ElementId panel = make(context, root, panel_params);
    LayoutParams child_params = fixed(10.0F, 10.0F);
    child_params.position = {5.0F, 5.0F};
    const ElementId child = make(context, panel, child_params);
    context.update_layout({200.0F, 100.0F});
    TRUFFLE_CHECK(at(context, panel) == (Rect{15.0F, 25.0F, 40.0F, 30.0F}));
    TRUFFLE_CHECK(at(context, child) == (Rect{7.0F, 5.0F, 10.0F, 10.0F}));
    TRUFFLE_CHECK(context.absolute_bounds(child).value() == (Rect{22.0F, 30.0F, 10.0F, 10.0F}));

    Context column;
    TRUFFLE_CHECK(column.set_layout(column.root(), stack(Axis::vertical)).ok());
    LayoutParams capped;
    capped.height = 10.0F;
    capped.max.width = 50.0F;
    const ElementId narrow = make(column, column.root(), capped);
    const ElementId wide = make(column, column.root(), fixed(500.0F, 10.0F));
    LayoutParams floor;
    floor.min = {0.0F, 25.0F};
    const ElementId tall = make(column, column.root(), floor);
    column.update_layout({200.0F, 100.0F});
    TRUFFLE_CHECK(at(column, narrow) == (Rect{0.0F, 0.0F, 50.0F, 10.0F}));
    TRUFFLE_CHECK(at(column, wide) == (Rect{0.0F, 10.0F, 200.0F, 10.0F}));
    TRUFFLE_CHECK(at(column, tall) == (Rect{0.0F, 20.0F, 200.0F, 25.0F}));
    return 0;
}

// Places children diagonally to prove applications can supply layouts.
class Diagonal final : public Layout {
public:
    Size measure(LayoutPass& pass, ElementId element, Constraints) const override {
        Size total;
        for (const ElementId child : pass.children(element)) {
            const Size size = pass.measure(child, {});
            total.width += size.width;
            total.height += size.height;
        }
        return total;
    }
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override {
        float x = content.x;
        float y = content.y;
        for (const ElementId child : pass.children(element)) {
            const Size size = pass.desired(child);
            pass.arrange(child, {x, y, size.width, size.height});
            x += size.width;
            y += size.height;
        }
    }
};

int test_custom_layout() {
    Context context;
    TRUFFLE_CHECK(context.set_layout(context.root(), std::make_shared<Diagonal>()).ok());
    const ElementId a = make(context, context.root(), fixed(10.0F, 5.0F));
    const ElementId b = make(context, context.root(), fixed(20.0F, 15.0F));
    context.update_layout({100.0F, 100.0F});
    TRUFFLE_CHECK(at(context, a) == (Rect{0.0F, 0.0F, 10.0F, 5.0F}));
    TRUFFLE_CHECK(at(context, b) == (Rect{10.0F, 5.0F, 20.0F, 15.0F}));
    return 0;
}

int test_incremental() {
    Context context;
    const ElementId root = context.root();
    TRUFFLE_CHECK(context.set_layout(root, stack(Axis::vertical)).ok());
    const ElementId row_a = make(context, root);
    const ElementId row_b = make(context, root);
    TRUFFLE_CHECK(context.set_layout(row_a, stack(Axis::horizontal)).ok());
    TRUFFLE_CHECK(context.set_layout(row_b, stack(Axis::horizontal)).ok());
    const ElementId a1 = make(context, row_a, fixed(10.0F, 10.0F));
    const ElementId a2 = make(context, row_a, fixed(10.0F, 10.0F));
    const ElementId a3 = make(context, row_a, fixed(10.0F, 10.0F));
    const ElementId b1 = make(context, row_b, fixed(10.0F, 10.0F));
    const ElementId b2 = make(context, row_b, fixed(10.0F, 10.0F));

    auto stats = context.update_layout({100.0F, 100.0F});
    TRUFFLE_CHECK(stats.measured == 8 && stats.arranged == 8);
    context.clear_dirty();

    // Idle: no layout work at all.
    stats = context.update_layout({100.0F, 100.0F});
    TRUFFLE_CHECK(stats.measured == 0 && stats.arranged == 0 && !context.has_dirty());

    // One leaf grows: only its path is measured; the moved sibling a3 keeps
    // its subtree and shapes, and row_b is untouched.
    TRUFFLE_CHECK(context.set_layout_params(a2, fixed(20.0F, 10.0F)).ok());
    stats = context.update_layout({100.0F, 100.0F});
    TRUFFLE_CHECK(stats.measured == 3 && stats.arranged == 3);
    TRUFFLE_CHECK(at(context, a2) == (Rect{10.0F, 0.0F, 20.0F, 10.0F}));
    TRUFFLE_CHECK(at(context, a3) == (Rect{30.0F, 0.0F, 10.0F, 10.0F}));
    TRUFFLE_CHECK(context.dirty_elements() == std::vector<ElementId>{a2});
    TRUFFLE_CHECK(context.dirty(a2) == Dirty::shape);
    context.clear_dirty();

    // Unchanged parameters are not a change.
    TRUFFLE_CHECK(context.set_layout_params(a1, fixed(10.0F, 10.0F)).ok());
    TRUFFLE_CHECK(!context.has_dirty());

    // A wider viewport re-measures only what depends on the width.
    stats = context.update_layout({120.0F, 100.0F});
    TRUFFLE_CHECK(stats.measured == 3);
    TRUFFLE_CHECK(at(context, row_b) == (Rect{0.0F, 10.0F, 120.0F, 10.0F}));
    TRUFFLE_CHECK(at(context, b1) == (Rect{0.0F, 0.0F, 10.0F, 10.0F}));
    TRUFFLE_CHECK(at(context, b2) == (Rect{10.0F, 0.0F, 10.0F, 10.0F}));
    context.clear_dirty();

    // Moving a free-placed element re-arranges only its parent.
    Context free;
    const ElementId box = make(free, free.root(), fixed(10.0F, 10.0F));
    free.update_layout({50.0F, 50.0F});
    free.clear_dirty();
    LayoutParams moved = fixed(10.0F, 10.0F);
    moved.position = {5.0F, 5.0F};
    TRUFFLE_CHECK(free.set_layout_params(box, moved).ok());
    TRUFFLE_CHECK(free.dirty(box) == Dirty::none);
    stats = free.update_layout({50.0F, 50.0F});
    TRUFFLE_CHECK(stats.measured == 0 && stats.arranged == 1);
    TRUFFLE_CHECK(at(free, box) == (Rect{5.0F, 5.0F, 10.0F, 10.0F}));
    TRUFFLE_CHECK(free.dirty(box) == Dirty::none);
    return 0;
}

int test_invalid_handles() {
    Context context;
    const ElementId gone = make(context, context.root());
    TRUFFLE_CHECK(context.destroy(gone).ok());
    TRUFFLE_CHECK(!context.bounds(gone).ok() && !context.absolute_bounds(gone).ok());
    TRUFFLE_CHECK(!context.set_layout(gone, stack(Axis::vertical)).ok());
    TRUFFLE_CHECK(!context.set_layout_params(gone, {}).ok());
    TRUFFLE_CHECK(!context.layout_params(gone).ok());
    TRUFFLE_CHECK(context.set_layout(context.root(), nullptr).ok());
    return 0;
}

} // namespace

int main() {
    if (const int failed = test_column_padding_spacing()) return failed;
    if (const int failed = test_row_alignment()) return failed;
    if (const int failed = test_stretching()) return failed;
    if (const int failed = test_min_max_and_free_placement()) return failed;
    if (const int failed = test_custom_layout()) return failed;
    if (const int failed = test_incremental()) return failed;
    return test_invalid_handles();
}

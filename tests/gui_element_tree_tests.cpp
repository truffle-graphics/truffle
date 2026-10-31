#include "test_support.hpp"
#include "truffle/gui/context.hpp"

#include <algorithm>
#include <vector>

namespace {

using truffle::gui::Context;
using truffle::gui::Dirty;
using truffle::gui::ElementId;

constexpr Dirty all = Dirty::style | Dirty::measure | Dirty::arrange | Dirty::shape;

bool same(std::span<const ElementId> actual, std::vector<ElementId> expected) {
    return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

ElementId make(Context& context, ElementId parent) {
    auto created = context.create(parent);
    return created.ok() ? created.value() : ElementId{};
}

int test_structure() {
    Context context;
    const ElementId root = context.root();
    TRUFFLE_CHECK(context.alive(root) && context.element_count() == 1);
    TRUFFLE_CHECK(context.parent(root).ok() && !context.parent(root).value().valid());

    const ElementId a = make(context, root);
    const ElementId b = make(context, root);
    const ElementId c = make(context, root);
    const ElementId a1 = make(context, a);
    TRUFFLE_CHECK(a.valid() && b.valid() && c.valid() && a1.valid());
    TRUFFLE_CHECK(context.element_count() == 5);
    TRUFFLE_CHECK(same(context.children(root), {a, b, c}));
    TRUFFLE_CHECK(context.parent(a1).value() == a);

    TRUFFLE_CHECK(context.reparent(c, root, 0).ok());
    TRUFFLE_CHECK(same(context.children(root), {c, a, b}));
    TRUFFLE_CHECK(context.reparent(c, root, 2).ok());
    TRUFFLE_CHECK(same(context.children(root), {a, b, c}));
    TRUFFLE_CHECK(!context.reparent(c, root, 3).ok());

    TRUFFLE_CHECK(context.reparent(b, a1).ok());
    TRUFFLE_CHECK(same(context.children(root), {a, c}));
    TRUFFLE_CHECK(same(context.children(a1), {b}) && context.parent(b).value() == a1);
    TRUFFLE_CHECK(context.reparent(b, a, 0).ok());
    TRUFFLE_CHECK(same(context.children(a), {b, a1}) && context.children(a1).empty());
    TRUFFLE_CHECK(!context.reparent(a, a1).ok());
    TRUFFLE_CHECK(!context.reparent(a, a).ok());
    TRUFFLE_CHECK(!context.reparent(root, a).ok());
    TRUFFLE_CHECK(!context.reparent(c, a, 3).ok());
    TRUFFLE_CHECK(same(context.children(root), {a, c}));
    return 0;
}

int test_handles() {
    Context context;
    const ElementId panel = make(context, context.root());
    const ElementId child = make(context, panel);
    const ElementId grandchild = make(context, child);
    TRUFFLE_CHECK(!context.destroy(context.root()).ok());
    TRUFFLE_CHECK(context.destroy(panel).ok());
    TRUFFLE_CHECK(context.element_count() == 1);
    TRUFFLE_CHECK(context.children(context.root()).empty());
    for (const ElementId removed : {panel, child, grandchild}) {
        TRUFFLE_CHECK(!context.alive(removed));
        TRUFFLE_CHECK(!context.parent(removed).ok());
        TRUFFLE_CHECK(context.children(removed).empty());
        TRUFFLE_CHECK(!context.invalidate(removed, Dirty::shape).ok());
        TRUFFLE_CHECK(!context.destroy(removed).ok());
        TRUFFLE_CHECK(!context.create(removed).ok());
        TRUFFLE_CHECK(!context.reparent(removed, context.root()).ok());
        TRUFFLE_CHECK(context.dirty(removed) == Dirty::none);
    }

    // Reused slots get new generations; stale handles stay dead.
    std::vector<ElementId> fresh;
    for (int i = 0; i < 3; ++i) {
        fresh.push_back(make(context, context.root()));
    }
    for (const ElementId element : fresh) {
        TRUFFLE_CHECK(context.alive(element));
        TRUFFLE_CHECK(element != panel && element != child && element != grandchild);
    }
    TRUFFLE_CHECK(!context.alive(panel) && !context.alive(child));
    TRUFFLE_CHECK(!context.alive(grandchild));
    TRUFFLE_CHECK(!context.alive(ElementId{}) && !context.create(ElementId{}).ok());
    TRUFFLE_CHECK(!context.alive(ElementId{12345}));
    return 0;
}

int test_invalidation() {
    Context context;
    const ElementId root = context.root();
    const ElementId panel = make(context, root);
    const ElementId row = make(context, panel);
    const ElementId label = make(context, row);
    const ElementId sibling = make(context, panel);
    TRUFFLE_CHECK(context.dirty(label) == all && context.has_dirty());
    context.clear_dirty();

    // Idle: nothing is dirty and nothing is reported.
    TRUFFLE_CHECK(!context.has_dirty() && context.dirty_elements().empty());
    for (const ElementId element : {root, panel, row, label, sibling}) {
        TRUFFLE_CHECK(context.dirty(element) == Dirty::none);
    }

    // A color change only repaints the element.
    TRUFFLE_CHECK(context.invalidate(label, Dirty::shape).ok());
    TRUFFLE_CHECK(context.dirty(label) == Dirty::shape);
    TRUFFLE_CHECK(context.dirty_elements() == std::vector<ElementId>{label});
    context.clear_dirty();

    // A style change is local too.
    TRUFFLE_CHECK(context.invalidate(sibling, Dirty::style).ok());
    TRUFFLE_CHECK(context.dirty_elements() == std::vector<ElementId>{sibling});
    context.clear_dirty();

    // A size change re-measures the path to the root and re-arranges it.
    TRUFFLE_CHECK(context.invalidate(label, Dirty::measure).ok());
    TRUFFLE_CHECK(context.dirty(label) ==
                  (Dirty::measure | Dirty::arrange | Dirty::shape));
    for (const ElementId ancestor : {row, panel, root}) {
        TRUFFLE_CHECK(context.dirty(ancestor) == (Dirty::measure | Dirty::arrange));
    }
    TRUFFLE_CHECK(context.dirty(sibling) == Dirty::none);
    TRUFFLE_CHECK(context.dirty_elements().size() == 4);

    // Clearing selected stages keeps the rest.
    context.clear_dirty(Dirty::measure | Dirty::arrange);
    TRUFFLE_CHECK(context.dirty_elements() == std::vector<ElementId>{label});
    TRUFFLE_CHECK(context.dirty(label) == Dirty::shape);
    context.clear_dirty();

    // Arrange marks the element's own shapes but not its ancestors.
    TRUFFLE_CHECK(context.invalidate(row, Dirty::arrange).ok());
    TRUFFLE_CHECK(context.dirty(row) == (Dirty::arrange | Dirty::shape));
    TRUFFLE_CHECK(context.dirty_elements().size() == 1);
    context.clear_dirty();
    TRUFFLE_CHECK(context.invalidate(row, Dirty::none).ok() && !context.has_dirty());

    // Structural changes mark the affected parents.
    TRUFFLE_CHECK(context.reparent(label, sibling).ok());
    TRUFFLE_CHECK(has(context.dirty(row), Dirty::measure));
    TRUFFLE_CHECK(has(context.dirty(sibling), Dirty::measure));
    TRUFFLE_CHECK(has(context.dirty(panel), Dirty::measure));
    context.clear_dirty();
    TRUFFLE_CHECK(context.destroy(label).ok());
    TRUFFLE_CHECK(has(context.dirty(sibling), Dirty::measure));
    TRUFFLE_CHECK(context.dirty(row) == Dirty::none);
    context.clear_dirty();
    const ElementId added = make(context, row);
    TRUFFLE_CHECK(context.dirty(added) == all && has(context.dirty(row), Dirty::measure));
    TRUFFLE_CHECK(context.dirty(sibling) == Dirty::none);

    // Destroyed elements leave the dirty list.
    TRUFFLE_CHECK(context.destroy(added).ok());
    const auto dirty = context.dirty_elements();
    TRUFFLE_CHECK(std::find(dirty.begin(), dirty.end(), added) == dirty.end());
    context.clear_dirty();
    TRUFFLE_CHECK(!context.has_dirty());
    return 0;
}

} // namespace

int main() {
    if (const int failed = test_structure()) return failed;
    if (const int failed = test_handles()) return failed;
    return test_invalidation();
}

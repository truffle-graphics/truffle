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
    TFL_CHECK(context.alive(root) && context.element_count() == 1);
    TFL_CHECK(context.parent(root).ok() && !context.parent(root).value().valid());

    const ElementId a = make(context, root);
    const ElementId b = make(context, root);
    const ElementId c = make(context, root);
    const ElementId a1 = make(context, a);
    TFL_CHECK(a.valid() && b.valid() && c.valid() && a1.valid());
    TFL_CHECK(context.element_count() == 5);
    TFL_CHECK(same(context.children(root), {a, b, c}));
    TFL_CHECK(context.parent(a1).value() == a);

    TFL_CHECK(context.reparent(c, root, 0).ok());
    TFL_CHECK(same(context.children(root), {c, a, b}));
    TFL_CHECK(context.reparent(c, root, 2).ok());
    TFL_CHECK(same(context.children(root), {a, b, c}));
    TFL_CHECK(!context.reparent(c, root, 3).ok());

    TFL_CHECK(context.reparent(b, a1).ok());
    TFL_CHECK(same(context.children(root), {a, c}));
    TFL_CHECK(same(context.children(a1), {b}) && context.parent(b).value() == a1);
    TFL_CHECK(context.reparent(b, a, 0).ok());
    TFL_CHECK(same(context.children(a), {b, a1}) && context.children(a1).empty());
    TFL_CHECK(!context.reparent(a, a1).ok());
    TFL_CHECK(!context.reparent(a, a).ok());
    TFL_CHECK(!context.reparent(root, a).ok());
    TFL_CHECK(!context.reparent(c, a, 3).ok());
    TFL_CHECK(same(context.children(root), {a, c}));
    return 0;
}

int test_handles() {
    Context context;
    const ElementId panel = make(context, context.root());
    const ElementId child = make(context, panel);
    const ElementId grandchild = make(context, child);
    TFL_CHECK(!context.destroy(context.root()).ok());
    TFL_CHECK(context.destroy(panel).ok());
    TFL_CHECK(context.element_count() == 1);
    TFL_CHECK(context.children(context.root()).empty());
    for (const ElementId removed : {panel, child, grandchild}) {
        TFL_CHECK(!context.alive(removed));
        TFL_CHECK(!context.parent(removed).ok());
        TFL_CHECK(context.children(removed).empty());
        TFL_CHECK(!context.invalidate(removed, Dirty::shape).ok());
        TFL_CHECK(!context.destroy(removed).ok());
        TFL_CHECK(!context.create(removed).ok());
        TFL_CHECK(!context.reparent(removed, context.root()).ok());
        TFL_CHECK(context.dirty(removed) == Dirty::none);
    }

    // Reused slots get new generations; stale handles stay dead.
    std::vector<ElementId> fresh;
    for (int i = 0; i < 3; ++i) {
        fresh.push_back(make(context, context.root()));
    }
    for (const ElementId element : fresh) {
        TFL_CHECK(context.alive(element));
        TFL_CHECK(element != panel && element != child && element != grandchild);
    }
    TFL_CHECK(!context.alive(panel) && !context.alive(child));
    TFL_CHECK(!context.alive(grandchild));
    TFL_CHECK(!context.alive(ElementId{}) && !context.create(ElementId{}).ok());
    TFL_CHECK(!context.alive(ElementId{12345}));
    return 0;
}

int test_invalidation() {
    Context context;
    const ElementId root = context.root();
    const ElementId panel = make(context, root);
    const ElementId row = make(context, panel);
    const ElementId label = make(context, row);
    const ElementId sibling = make(context, panel);
    TFL_CHECK(context.dirty(label) == all && context.has_dirty());
    context.clear_dirty();

    // Idle: nothing is dirty and nothing is reported.
    TFL_CHECK(!context.has_dirty() && context.dirty_elements().empty());
    for (const ElementId element : {root, panel, row, label, sibling}) {
        TFL_CHECK(context.dirty(element) == Dirty::none);
    }

    // A color change only repaints the element.
    TFL_CHECK(context.invalidate(label, Dirty::shape).ok());
    TFL_CHECK(context.dirty(label) == Dirty::shape);
    TFL_CHECK(context.dirty_elements() == std::vector<ElementId>{label});
    context.clear_dirty();

    // A style change is local too.
    TFL_CHECK(context.invalidate(sibling, Dirty::style).ok());
    TFL_CHECK(context.dirty_elements() == std::vector<ElementId>{sibling});
    context.clear_dirty();

    // A size change re-measures the path to the root and re-arranges it.
    TFL_CHECK(context.invalidate(label, Dirty::measure).ok());
    TFL_CHECK(context.dirty(label) ==
                  (Dirty::measure | Dirty::arrange | Dirty::shape));
    for (const ElementId ancestor : {row, panel, root}) {
        TFL_CHECK(context.dirty(ancestor) == (Dirty::measure | Dirty::arrange));
    }
    TFL_CHECK(context.dirty(sibling) == Dirty::none);
    TFL_CHECK(context.dirty_elements().size() == 4);

    // Clearing selected stages keeps the rest.
    context.clear_dirty(Dirty::measure | Dirty::arrange);
    TFL_CHECK(context.dirty_elements() == std::vector<ElementId>{label});
    TFL_CHECK(context.dirty(label) == Dirty::shape);
    context.clear_dirty();

    // Arrange marks the element's own shapes but not its ancestors.
    TFL_CHECK(context.invalidate(row, Dirty::arrange).ok());
    TFL_CHECK(context.dirty(row) == (Dirty::arrange | Dirty::shape));
    TFL_CHECK(context.dirty_elements().size() == 1);
    context.clear_dirty();
    TFL_CHECK(context.invalidate(row, Dirty::none).ok() && !context.has_dirty());

    // Structural changes mark the affected parents.
    TFL_CHECK(context.reparent(label, sibling).ok());
    TFL_CHECK(has(context.dirty(row), Dirty::measure));
    TFL_CHECK(has(context.dirty(sibling), Dirty::measure));
    TFL_CHECK(has(context.dirty(panel), Dirty::measure));
    context.clear_dirty();
    TFL_CHECK(context.destroy(label).ok());
    TFL_CHECK(has(context.dirty(sibling), Dirty::measure));
    TFL_CHECK(context.dirty(row) == Dirty::none);
    context.clear_dirty();
    const ElementId added = make(context, row);
    TFL_CHECK(context.dirty(added) == all && has(context.dirty(row), Dirty::measure));
    TFL_CHECK(context.dirty(sibling) == Dirty::none);

    // Destroyed elements leave the dirty list.
    TFL_CHECK(context.destroy(added).ok());
    const auto dirty = context.dirty_elements();
    TFL_CHECK(std::find(dirty.begin(), dirty.end(), added) == dirty.end());
    context.clear_dirty();
    TFL_CHECK(!context.has_dirty());
    return 0;
}

} // namespace

int main() {
    if (const int failed = test_structure()) return failed;
    if (const int failed = test_handles()) return failed;
    return test_invalidation();
}

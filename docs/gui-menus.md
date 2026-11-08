# Reusable GUI Menus (#269)

Menus are part of the fluent consumer API in `truffle/gui/ui.hpp`
(`Truffle::GUI`): `MenuBar`, `Menu` and `MenuItem` handles. There is no
application, workspace, windowing or GUI Dock dependency. The application still
owns actions, selected values, shortcut bindings, fonts/atlases, the host loop
and native rendering.

## Composition

Handles are cheap, non-owning values; dropping one leaves the Context-owned
elements intact. Failures go to the `Ui` error handler instead of being returned
from every call (see the GUI API guide in the `Ui` header).

```cpp
gui::Ui ui{context, {&text_engine, font, atlas_id}};
auto bar = ui.root().menu_bar();
auto actions = bar.menu("Actions");          // creates and wires its trigger
actions.item("Run")
    .shortcut("Ctrl+R")                       // display, not a key binding
    .on_click(run_action);
actions.menu("More").item("Run again").on_click(run_action);
actions.separator();
actions.item("Add mode...").enabled(false);

auto modes = ui.root().menu_button("Mode");  // standalone dropdown button
modes.item("Wireframe").checkable(true).on_change([](bool on) { /* ... */ });
```

A menu-bar trigger is always present, even with only one menu. A standalone
menu button includes a downward indicator; menu-bar triggers do not.
`Menu::open()` and `Menu::close()` drive the popup programmatically.

`text`, `shortcut`, `enabled` and `checked` update persistent entries and never
invoke application callbacks. A `checkable` item toggles its own check when
activated and reports the new value through `on_change` before `on_click`.
Radio-style exclusive choices remain an application policy: update sibling
items with `checked(...)` from the callback.

[`examples/gui_menus/menu_example.hpp`](../examples/gui_menus/menu_example.hpp)
is a host-independent composition example compiled by the UI tests. It is not
a second native host or a Studio integration.

## Interaction And Lifetime

One open chain per host surface is shared by menu bars and standalone buttons.
Opening a different root closes the previous root and all its descendants.
Clicking an enabled item activates its action or opens its submenu; disabled
and non-actionable entries never activate. Action activation closes the chain
before invoking the copied callback, so that callback may destroy the menu or
its trigger safely.

Submenus open after **250 ms** of pointer hover, using element-owned one-shot
timers rather than a perpetual live updater. Movement toward an existing child
popup preserves its branch for a **300 ms** travel grace, using the triangle
from the last parent-row pointer position to the child's near edge. Entering
the child cancels a pending sibling switch. Hovering another enabled menu-bar
trigger switches the root immediately. Opening a sibling closes the old sibling
and its descendants.
Entering the protected travel triangle extends an earlier sibling-hover
deadline when needed. Disabled/removed pending rows cancel their timer.

Up/Down wrap through actionable enabled rows; Home/End select the first/last.
Enter/Space activate without repeat activation. Right opens a submenu, or
traverses the menu bar at the root; Left returns from a submenu, or traverses
the bar. Escape closes only the deepest open popup first. Tab dismisses the
chain and resumes ordinary focus traversal. Closed, focused menu-bar triggers
support Left/Right traversal and Down/Up/Enter or a completed Space gesture to
open their menu.

Outside clicks dismiss before normal routing, so the same click can still
activate its underlying control. Focus moving outside the chain, host focus
loss and pointer cancellation also dismiss. Dismissal restores the original
eligible focus, falling back to the trigger; an outside click retains normal
target focus instead. Disabling a focused row/branch selects another eligible
row, or the menu panel when no actionable row remains.

Menus remain attached under their triggers. Destroying a trigger or ancestor
subtree destroys its menus; destroying only a menu disables its now-inert
trigger. Destroying a submenu removes its parent's arrow/navigation eligibility.
Checked references become stale on element or Context destruction, and timers
are removed with their owner. Whole menu-button/bar subtrees can move to another
`Surface`; opening there joins that surface's chain. Native surface/window
ownership remains with the application.

## Layout, Theme And Host Contracts

Popups use parent-anchored overlays, not another drawing or hit-testing tree.
Root menus prefer below their button; submenus prefer the parent's right edge.
Insufficient room flips to the opposite side, then shifts/clamps to the logical
viewport. Placement is refreshed after ordinary layout, including resize,
anchor movement and scrolling. Oversized menus are viewport-clipped and
keyboard selection scrolls the selected row into view. By default a menu has a
160-unit minimum width when the viewport permits it; rows are at least 28 units
tall and separators are 9 units tall. These are component defaults, not fixed
layout requirements; [component styles](gui-styling.md) can override them.

Panels and controls reuse `gui.surface`, `gui.control`, `gui.hover`,
`gui.pressed`, `gui.disabled`, `gui.border`, `gui.border_width`, `gui.radius`,
`gui.padding` and `gui.text`. Disabled labels/indicators use the new
`gui.text_disabled` token in both built-in themes. Checks and arrows are solid
GUI shapes, not platform glyphs or icon-font dependencies. Labels/shortcut
display use the application's `TextEngine`, font and atlas, with the same
requirements as other text widgets.
Menu item styles also expose indicator size, column widths and gaps; standalone
menu buttons expose their indicator size, trailing width and gap. Per-item and
per-button styles reach their internal labels and indicators.

The shared GUI extensions are:

- `Context::set_visible`: collapse a subtree out of layout, drawing and input.
- `Context::set_popup`: parent-anchored, viewport-aware overlay placement.
- `Context::set_input_filter`: surface-local, element-owned input before
  bubbling, without replacing existing application event handlers.
- `Context::request_update_at`: schedule/cancel an element's updater once.
  `Context::time()` reports the host time; `handle(event, now)` and
  `Surface::handle(event, now)` allow deterministic input timing.

Hosts retain the normal `needs_frame` / `next_update` / `prepare_frame(now)`
contract. Idle menus perform no style, layout, shape or updater recomputation.
These are unreleased API extensions, not an architectural layer change.
Rebuild GUI consumers when adopting them: `Context` storage and the input
function signatures changed; old compiled libraries cannot be mixed with the
new headers.

## Focused Evidence

`truffle_gui_ui_tests` covers fluent composition and self-toggling items;
`truffle_gui_menu_tests` covers the internal menu objects, pointer/keyboard behavior,
exact timer boundaries, safe submenu travel, focus/dismissal, disabled and
checked rows, viewport flips/resize/movement, overflow selection, cleanup,
Context teardown, and independent/moved surfaces.

`truffle_gui_drawing_vulkan_tests` uses the prepared Painter program from #268
and requires active Khronos validation. Its shared native fixture checks exact
512x240 menu pixel probes, checkmark/arrow ink, disabled glyph/shortcut colors,
retained-frame byte equality, recursive overlay placement and action dismissal.
The same fixture belongs to the Metal native target, but Windows validation
does not imply a macOS rerun or broader backend maturity.

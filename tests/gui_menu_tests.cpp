#include "gui_text_fixture.hpp"
#include "menu_example.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/draw.hpp"
#include "truffle/gui/menus.hpp"
#include "truffle/gui/surface.hpp"
#include "truffle/gui/widgets.hpp"

#include <chrono>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
using namespace truffle;
using namespace truffle::gui;
using namespace std::chrono_literals;
using Clock = Context::Clock;
const auto t0 = Clock::time_point{1000ms};
static_assert(std::is_constructible_v<examples::MenuExample, Context &, ElementId,
                                      MenuTextStyle, std::function<void()>>);

void place(Context &context, ElementId element, Rect rect) {
  LayoutParams params;
  params.position = {rect.x, rect.y};
  params.width = rect.width;
  params.height = rect.height;
  tests::require(context.set_layout_params(element, params));
}

void frame(Context &context, Clock::time_point now = t0) {
  tests::require(context.prepare_frame(now).status);
}

void click(Context &context, Point point, Clock::time_point now = t0) {
  tests::require(context.handle(PointerEvent{.action = PointerAction::down,
                                             .position = point,
                                             .button = PointerButton::primary},
                                now));
  tests::require(context.handle(PointerEvent{.action = PointerAction::up,
                                             .position = point,
                                             .button = PointerButton::primary},
                                now));
}

void key(Context &context, Key key, Clock::time_point now = t0) {
  tests::require(context.handle(KeyEvent{.key = key}, now));
}

Point center(Context &context, ElementId element) {
  const auto rect = tests::require(context.absolute_bounds(element));
  return {rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F};
}

bool within(Rect inner, Size view) {
  return inner.x >= 0 && inner.y >= 0 && inner.x + inner.width <= view.width &&
         inner.y + inner.height <= view.height;
}

int keyboard_and_composition() {
  Context context;
  tests::require(context.set_viewport({640, 400}));
  auto bar = tests::require(MenuBar::create(context, context.root()));
  place(context, bar.element(), {8, 8, 300, 28});
  auto first = tests::require(bar.add_menu(""));
  auto second = tests::require(bar.add_menu(""));
  auto third_menu = tests::require(Menu::create(context, context.root()));
  auto third_button = tests::require(bar.add_menu("", third_menu));
  int actions = 0;
  auto a = tests::require(first.add_item({.activated = [&] { ++actions; }}));
  auto separator = tests::require(first.add_separator());
  auto disabled = tests::require(
      first.add_item({.enabled = false, .activated = [&] { actions += 100; }}));
  auto child = tests::require(first.add_submenu(""));
  auto leaf =
      tests::require(child.add_item({.checked = true, .activated = [&] { ++actions; }}));
  auto grandchild = tests::require(child.add_submenu(""));
  auto deep = tests::require(grandchild.add_item({.activated = [&] { ++actions; }}));
  auto b = tests::require(second.add_item({.activated = [&] { ++actions; }}));
  auto c = tests::require(third_menu.add_item({.activated = [&] { ++actions; }}));
  frame(context);
  TFL_CHECK(!first.is_open() && !context.visible(first.element()));
  TFL_CHECK(context.parent(first.element()).value() == first.trigger());
  TFL_CHECK(context.parent(first.trigger()).value() == bar.element());
  TFL_CHECK(context.semantics(bar.element()).value().role == Context::Role::menu_bar);
  TFL_CHECK(context.semantics(separator.element()).value().role ==
            Context::Role::menu_separator);
  tests::require(context.focus(first.trigger()));
  key(context, Key::down);
  frame(context);
  TFL_CHECK(first.is_open() && context.focused() == a.element());
  key(context, Key::down);
  TFL_CHECK(context.focused() == child.trigger());
  key(context, Key::right);
  frame(context);
  TFL_CHECK(child.is_open() && context.focused() == leaf.element());
  key(context, Key::down);
  TFL_CHECK(context.focused() == grandchild.trigger());
  key(context, Key::right);
  frame(context);
  TFL_CHECK(grandchild.is_open() && context.focused() == deep.element());
  key(context, Key::escape);
  TFL_CHECK(!grandchild.is_open() && child.is_open() && first.is_open());
  TFL_CHECK(context.focused() == grandchild.trigger());
  key(context, Key::left);
  TFL_CHECK(!child.is_open() && first.is_open() && context.focused() == child.trigger());
  key(context, Key::home);
  TFL_CHECK(context.focused() == a.element());
  key(context, Key::up);
  TFL_CHECK(context.focused() == child.trigger());
  key(context, Key::down);
  TFL_CHECK(context.focused() == a.element());
  key(context, Key::right);
  TFL_CHECK(!first.is_open() && second.is_open() && context.focused() == b.element());
  tests::require(third_button.set_enabled(false));
  key(context, Key::right);
  TFL_CHECK(first.is_open() && !second.is_open());
  key(context, Key::left);
  TFL_CHECK(second.is_open() && context.focused() == b.element());
  tests::require(context.handle(KeyEvent{.key = Key::enter, .repeat = true}, t0));
  TFL_CHECK(actions == 0 && second.is_open());
  key(context, Key::enter);
  TFL_CHECK(actions == 1 && !second.is_open() && context.focused() == first.trigger());
  tests::require(third_button.set_enabled(true));
  tests::require(context.focus(third_button.element()));
  key(context, Key::up);
  TFL_CHECK(third_menu.is_open() && context.focused() == c.element());
  key(context, Key::escape);
  TFL_CHECK(!third_menu.is_open() && context.focused() == third_button.element());
  tests::require(context.focus(first.trigger()));
  key(context, Key::right);
  TFL_CHECK(context.focused() == second.trigger() && !second.is_open());
  key(context, Key::down);
  key(context, Key::tab);
  TFL_CHECK(!second.is_open() && context.focused() == third_button.element());
  tests::require(first.open());
  frame(context);
  tests::require(
      context.handle(PointerEvent{.position = center(context, second.trigger())}, t0));
  TFL_CHECK(!first.is_open() && second.is_open());
  tests::require(second.close());
  auto dropdown = tests::require(MenuButton::create(context, context.root(), ""));
  place(context, dropdown.element(), {400, 8, 80, 28});
  tests::require(tests::require(dropdown.menu()).add_item({.activated = [] {}}));
  tests::require(first.open());
  tests::require(dropdown.open());
  TFL_CHECK(!first.is_open() && tests::require(dropdown.menu()).is_open());
  tests::require(context.focus(first.trigger()));
  TFL_CHECK(!tests::require(dropdown.menu()).is_open());
  tests::require(first.open());
  key(context, Key::down);
  key(context, Key::right);
  key(context, Key::down);
  key(context, Key::right);
  tests::require(context.focus(leaf.element()));
  key(context, Key::left);
  TFL_CHECK(first.is_open() && !child.is_open() && !grandchild.is_open());
  key(context, Key::right);
  key(context, Key::down);
  key(context, Key::right);
  tests::require(context.focus(a.element()));
  key(context, Key::escape);
  TFL_CHECK(first.is_open() && child.is_open() && !grandchild.is_open());
  tests::require(context.focus(a.element()));
  key(context, Key::right);
  TFL_CHECK(!first.is_open() && !child.is_open() && second.is_open());
  tests::require(second.close());
  tests::require(first.open());
  key(context, Key::down);
  key(context, Key::right);
  tests::require(context.focus(first.trigger()));
  key(context, Key::down);
  TFL_CHECK(first.is_open() && !child.is_open() && context.focused() == a.element());
  tests::require(first.close());
  TFL_CHECK(!disabled.checked().value().has_value());
  TFL_CHECK(leaf.checked().value() == std::optional<bool>{true});
  tests::require(leaf.set_checked(false));
  TFL_CHECK(actions == 1 && leaf.checked().value() == std::optional<bool>{false});
  tests::require(leaf.set_checked(std::nullopt));
  TFL_CHECK(!leaf.checked().value().has_value());
  frame(context);
  const auto idle = context.prepare_frame(t0 + 1s);
  TFL_CHECK(idle.status.ok() && idle.updates == 0 && idle.styles.resolved == 0 &&
            idle.layout.measured == 0 && idle.layout.arranged == 0 &&
            idle.shapes.regenerated == 0);
  TFL_CHECK(!context.needs_frame(t0 + 1s) && !context.next_update());
  return 0;
}

int pointer_and_delays() {
  Context context;
  tests::require(context.set_viewport({640, 400}));
  auto button = tests::require(MenuButton::create(context, context.root(), ""));
  place(context, button.element(), {10, 10, 80, 28});
  auto menu = tests::require(button.menu());
  auto first = tests::require(menu.add_submenu(""));
  auto second = tests::require(menu.add_submenu(""));
  int actions = 0;
  auto a = tests::require(first.add_item({.activated = [&] { ++actions; }}));
  auto b = tests::require(second.add_item({.activated = [&] { ++actions; }}));
  auto disabled = tests::require(menu.add_item(
      {.enabled = false, .checked = true, .activated = [&] { actions += 100; }}));
  auto action = tests::require(menu.add_item({.activated = [&] { ++actions; }}));
  int outside_actions = 0;
  auto outside = tests::require(Widget::create(
      context, context.root(),
      {.kind = WidgetKind::button, .activated = [&] { ++outside_actions; }}));
  place(context, outside.element(), {400, 250, 100, 32});
  frame(context);
  click(context, center(context, button.element()));
  frame(context);
  TFL_CHECK(menu.is_open() && !context.captured().valid());
  tests::require(
      context.handle(PointerEvent{.position = center(context, first.trigger())}, t0));
  TFL_CHECK(context.next_update() == t0 + 250ms);
  frame(context, t0 + 249ms);
  TFL_CHECK(!first.is_open());
  frame(context, t0 + 250ms);
  TFL_CHECK(first.is_open() && !second.is_open() && context.focused() == a.element());

  const auto parent_rect = tests::require(context.absolute_bounds(first.trigger()));
  const auto sibling_rect = tests::require(context.absolute_bounds(second.trigger()));
  const auto child_rect = tests::require(context.absolute_bounds(first.element()));
  const Point origin{parent_rect.x + 24, parent_rect.y + parent_rect.height * 0.5F};
  tests::require(context.handle(PointerEvent{.position = origin}, t0 + 500ms));
  tests::require(context.handle(
      PointerEvent{.position = {sibling_rect.x + 10, sibling_rect.y + 10}}, t0 + 501ms));
  TFL_CHECK(context.next_update() == t0 + 751ms);
  const Point diagonal{parent_rect.x + parent_rect.width - 4, sibling_rect.y + 2};
  tests::require(context.handle(PointerEvent{.position = diagonal}, t0 + 550ms));
  TFL_CHECK(first.is_open() && !second.is_open());
  TFL_CHECK(context.next_update() == t0 + 800ms);
  frame(context, t0 + 751ms);
  TFL_CHECK(first.is_open() && !second.is_open());
  tests::require(context.handle(
      PointerEvent{.position = {child_rect.x + 20, child_rect.y + 20}}, t0 + 752ms));
  frame(context, t0 + 900ms);
  TFL_CHECK(first.is_open() && !second.is_open() && !context.next_update());
  tests::require(context.handle(
      PointerEvent{.position = {sibling_rect.x + 10, sibling_rect.y + 10}}, t0 + 1s));
  frame(context, t0 + 1249ms);
  TFL_CHECK(first.is_open() && !second.is_open());
  frame(context, t0 + 1250ms);
  TFL_CHECK(!first.is_open() && second.is_open() && context.focused() == b.element());
  click(context, center(context, b.element()), t0 + 1300ms);
  TFL_CHECK(actions == 1 && !menu.is_open() && !second.is_open());

  click(context, center(context, button.element()), t0 + 2s);
  frame(context, t0 + 2s);
  click(context, center(context, disabled.element()), t0 + 2s);
  TFL_CHECK(actions == 1 && menu.is_open() &&
            disabled.checked().value() == std::optional<bool>{true});
  click(context, center(context, first.trigger()), t0 + 2s);
  frame(context, t0 + 2s);
  TFL_CHECK(first.is_open());
  click(context, center(context, second.trigger()), t0 + 2s);
  frame(context, t0 + 2s);
  TFL_CHECK(!first.is_open() && second.is_open());
  click(context, center(context, outside.element()), t0 + 2s);
  TFL_CHECK(!menu.is_open() && !second.is_open() && outside_actions == 1);
  TFL_CHECK(context.focused() == outside.element());

  click(context, center(context, button.element()), t0 + 3s);
  frame(context, t0 + 3s);
  tests::require(
      context.handle(PointerEvent{.action = PointerAction::down,
                                  .position = center(context, action.element()),
                                  .button = PointerButton::primary},
                     t0 + 3s));
  tests::require(context.handle(PointerEvent{.action = PointerAction::up,
                                             .position = {620, 380},
                                             .button = PointerButton::primary},
                                t0 + 3s));
  TFL_CHECK(actions == 1 && menu.is_open());
  tests::require(
      context.handle(PointerEvent{.action = PointerAction::up,
                                  .position = center(context, action.element()),
                                  .button = PointerButton::primary},
                     t0 + 3s));
  TFL_CHECK(actions == 1 && menu.is_open());
  tests::require(context.handle(FocusEvent{false}, t0 + 3s));
  TFL_CHECK(!menu.is_open() && !context.focused().valid() && !context.next_update());
  return 0;
}

int placement_and_overflow() {
  Context context;
  tests::require(context.set_viewport({480, 320}));
  auto button = tests::require(MenuButton::create(context, context.root(), ""));
  place(context, button.element(), {400, 286, 72, 28});
  auto menu = tests::require(button.menu());
  auto child = tests::require(menu.add_submenu(""));
  auto child_action = tests::require(child.add_item({.activated = [] {}}));
  tests::require(menu.add_item({.activated = [] {}}));
  frame(context);
  tests::require(button.open());
  frame(context);
  const auto root_rect = tests::require(context.absolute_bounds(menu.element()));
  TFL_CHECK(within(root_rect, context.viewport()) && root_rect.y < 286);
  key(context, Key::right);
  frame(context);
  const auto child_rect = tests::require(context.absolute_bounds(child.element()));
  const auto anchor = tests::require(context.absolute_bounds(child.trigger()));
  TFL_CHECK(within(child_rect, context.viewport()) &&
            child_rect.x + child_rect.width <= anchor.x);
  tests::require(context.set_viewport({220, 120}));
  frame(context);
  TFL_CHECK(within(context.absolute_bounds(menu.element()).value(), context.viewport()));
  TFL_CHECK(within(context.absolute_bounds(child.element()).value(), context.viewport()));
  tests::require(button.close());
  place(context, button.element(), {12, 8, 72, 28});
  frame(context);
  tests::require(button.open());
  frame(context);
  TFL_CHECK(context.absolute_bounds(menu.element()).value().x == 12);
  TFL_CHECK(context.absolute_bounds(menu.element()).value().y == 36);
  place(context, button.element(), {130, 8, 72, 28});
  frame(context);
  TFL_CHECK(context.absolute_bounds(menu.element()).value().x == 60);

  tests::require(button.close());
  for (int i = 0; i < 10; ++i) {
    tests::require(menu.add_item({.activated = [] {}}));
  }
  tests::require(button.open());
  frame(context);
  key(context, Key::end);
  frame(context);
  const auto selected = tests::require(context.absolute_bounds(context.focused()));
  const auto popup = tests::require(context.absolute_bounds(menu.element()));
  const auto padding = tests::require(context.layout_params(menu.element())).padding;
  TFL_CHECK(within(popup, context.viewport()));
  TFL_CHECK(selected.y >= popup.y + padding.top &&
            selected.y + selected.height <= popup.y + popup.height - padding.bottom);
  TFL_CHECK(context.child_offset(menu.element()).y < 0);
  key(context, Key::home);
  frame(context);
  TFL_CHECK(context.child_offset(menu.element()).y == 0);
  TFL_CHECK(context.focused() == child.trigger());
  TFL_CHECK(!child_action.submenu().ok());
  return 0;
}

int lifetime_and_contracts() {
  Menu retained;
  MenuButton retained_button;
  MenuItem retained_item;
  {
    auto context = std::make_unique<Context>();
    tests::require(context->set_viewport({400, 300}));
    retained_button = tests::require(MenuButton::create(*context, context->root(), ""));
    retained = tests::require(retained_button.menu());
    retained_item = tests::require(retained.add_item({.activated = [] {}}));
    frame(*context);
    tests::require(retained_button.open());
    frame(*context);
    TFL_CHECK(retained.is_open());
  }
  TFL_CHECK(!retained.alive() && !retained.is_open() && !retained.open().ok());
  TFL_CHECK(!retained_button.alive() && !retained_button.open().ok());
  TFL_CHECK(!retained_item.alive() && !retained_item.set_enabled(true).ok());

  Context context;
  tests::require(context.set_viewport({400, 300}));
  auto parent = tests::require(context.create(context.root()));
  auto button = tests::require(MenuButton::create(context, parent, ""));
  auto menu = tests::require(button.menu());
  auto child = tests::require(menu.add_submenu(""));
  auto leaf = tests::require(child.add_item({.activated = [] {}}));
  frame(context);
  tests::require(button.open());
  frame(context);
  tests::require(
      context.handle(PointerEvent{.position = center(context, child.trigger())}, t0));
  TFL_CHECK(context.next_update());
  tests::require(context.destroy(parent));
  TFL_CHECK(!button.alive() && !menu.alive() && !child.alive() && !leaf.alive());
  TFL_CHECK(!context.next_update() && !context.focused().valid() &&
            !context.captured().valid());
  frame(context, t0 + 1s);

  auto replacement = tests::require(MenuButton::create(context, context.root(), ""));
  auto replacement_menu = tests::require(replacement.menu());
  auto row = tests::require(replacement_menu.add_item({}));
  auto nested = tests::require(Menu::create(context, context.root()));
  tests::require(nested.add_item({.activated = [] {}}));
  tests::require(row.set_submenu(nested));
  TFL_CHECK(!row.set_action([] {}).ok());
  TFL_CHECK(!MenuButton::create(context, context.root(), "", nested).ok());
  TFL_CHECK(!row.set_submenu(replacement_menu).ok());
  tests::require(replacement.open());
  frame(context);
  key(context, Key::right);
  frame(context);
  TFL_CHECK(nested.is_open());
  tests::require(context.destroy(nested.element()));
  TFL_CHECK(!nested.alive() && replacement_menu.is_open() && !row.submenu().ok());
  tests::require(context.destroy(replacement_menu.element()));
  TFL_CHECK(!replacement_menu.alive() && !replacement.open().ok());
  TFL_CHECK(!context.interaction(replacement.element()).value().enabled);
  TFL_CHECK(!replacement.set_enabled(true).ok());
  frame(context);
  TFL_CHECK(!context.next_update());

  auto callback_button = tests::require(MenuButton::create(context, context.root(), ""));
  auto callback_menu = tests::require(callback_button.menu());
  tests::require(callback_menu.add_item({.activated = [&] {
    tests::require(context.destroy(callback_button.element()));
  }}));
  tests::require(callback_button.open());
  frame(context);
  key(context, Key::enter);
  TFL_CHECK(!callback_button.alive() && !callback_menu.alive());
  auto unattached = tests::require(Menu::create(context, context.root()));
  TFL_CHECK(!unattached.open().ok());
  Context foreign;
  TFL_CHECK(!MenuButton::create(foreign, foreign.root(), "", unattached).ok());
  return 0;
}

int dynamic_entries() {
  Context context;
  tests::require(context.set_viewport({500, 300}));
  auto button = tests::require(MenuButton::create(context, context.root(), ""));
  auto menu = tests::require(button.menu());
  auto branch = tests::require(menu.add_item({}));
  auto child = tests::require(Menu::create(context, context.root()));
  tests::require(branch.set_submenu(child));
  auto leaf = tests::require(child.add_item({.activated = [] {}}));
  auto next = tests::require(menu.add_item({.activated = [] {}}));
  tests::require(button.open());
  frame(context);
  tests::require(
      context.handle(PointerEvent{.position = center(context, branch.element())}, t0));
  TFL_CHECK(context.next_update() == t0 + 250ms);
  tests::require(branch.set_enabled(false));
  TFL_CHECK(menu.is_open() && context.focused() == next.element() &&
            !context.next_update());
  tests::require(branch.set_enabled(true));
  key(context, Key::home);
  key(context, Key::right);
  frame(context);
  TFL_CHECK(child.is_open() && context.focused() == leaf.element());
  tests::require(branch.set_enabled(false));
  TFL_CHECK(!child.is_open() && menu.is_open() && context.focused() == next.element());
  tests::require(branch.set_enabled(true));
  key(context, Key::home);
  key(context, Key::right);
  frame(context);
  tests::require(
      context.handle(PointerEvent{.position = center(context, branch.element())}, t0));
  TFL_CHECK(context.focused() == branch.element() && child.is_open());
  key(context, Key::right);
  TFL_CHECK(context.focused() == leaf.element() && child.is_open());
  key(context, Key::left);
  tests::require(
      context.handle(PointerEvent{.position = center(context, branch.element())}, t0));
  TFL_CHECK(context.next_update() == t0 + 250ms);
  tests::require(context.destroy(branch.element()));
  TFL_CHECK(menu.is_open() && context.focused() == next.element() && !child.alive() &&
            !context.next_update());
  tests::require(next.set_action({}));
  TFL_CHECK(menu.is_open() && context.focused() == menu.element());
  tests::require(next.set_action([] {}));
  key(context, Key::down);
  TFL_CHECK(context.focused() == next.element());
  tests::require(context.destroy(next.element()));
  TFL_CHECK(menu.is_open() && context.focused() == menu.element());
  key(context, Key::down);
  TFL_CHECK(context.focused() == menu.element());
  key(context, Key::escape);
  TFL_CHECK(!menu.is_open());
  return 0;
}

int surfaces_and_host_mechanisms() {
  Context context;
  tests::require(context.set_viewport({400, 300}));
  auto container = tests::require(context.create(context.root()));
  place(context, container, {0, 0, 200, 180});
  auto button = tests::require(MenuButton::create(context, container, ""));
  place(context, button.element(), {10, 10, 60, 28});
  auto menu = tests::require(button.menu());
  tests::require(menu.add_item({.activated = [] {}}));
  auto surface = tests::require(Surface::create(context));
  tests::require(surface->set_viewport({200, 180}));
  tests::require(surface->attach(container));
  auto moved_submenu = tests::require(menu.add_submenu(""));
  tests::require(moved_submenu.add_item({.activated = [] {}}));
  frame(context);
  for (const auto action : {PointerAction::down, PointerAction::up}) {
    tests::require(surface->handle(PointerEvent{.action = action,
                                                .position = {20, 20},
                                                .button = PointerButton::primary},
                                   t0));
  }
  frame(context);
  TFL_CHECK(menu.is_open() && !context.focused().valid());
  TFL_CHECK(within(context.absolute_bounds(menu.element()).value(), {200, 180}));
  tests::require(surface->handle(KeyEvent{.key = Key::down}, t0));
  tests::require(surface->handle(KeyEvent{.key = Key::right}, t0));
  frame(context);
  TFL_CHECK(moved_submenu.is_open());
  tests::require(surface->handle(KeyEvent{.key = Key::escape}, t0));
  TFL_CHECK(menu.is_open() && !moved_submenu.is_open());
  tests::require(surface->handle(KeyEvent{.key = Key::escape}, t0));
  TFL_CHECK(!menu.is_open());
  surface.reset();
  TFL_CHECK(!button.alive() && !menu.alive());

  auto source = tests::require(Surface::create(context));
  auto destination = tests::require(Surface::create(context));
  tests::require(source->set_viewport({400, 200}));
  tests::require(destination->set_viewport({400, 200}));
  auto bar = tests::require(MenuBar::create(context, source->root()));
  place(context, bar.element(), {0, 0, 200, 28});
  auto first = tests::require(bar.add_menu(""));
  auto second = tests::require(bar.add_menu(""));
  auto first_action = tests::require(first.add_item({.activated = [] {}}));
  auto second_action = tests::require(second.add_item({.activated = [] {}}));
  auto empty_bar = tests::require(MenuBar::create(context, source->root()));
  place(context, empty_bar.element(), {0, 40, 200, 28});
  tests::require(destination->attach(bar.element()));
  tests::require(destination->attach(empty_bar.element()));
  source.reset();
  TFL_CHECK(bar.alive() && empty_bar.alive() && first.alive() && second.alive());
  auto added_later = tests::require(empty_bar.add_menu(""));
  tests::require(added_later.add_item({.activated = [] {}}));
  tests::require(first.open());
  frame(context);
  bool destination_focus = false;
  context.set_event_trace([&](const RoutedEvent &event) {
    if (const auto *focus = std::get_if<ElementFocusEvent>(&event.payload);
        focus && focus->focused && event.current == second_action.element()) {
      destination_focus = context.focused() == second_action.element();
    }
  });
  tests::require(destination->handle(KeyEvent{.key = Key::right}, t0));
  context.set_event_trace({});
  TFL_CHECK(!first.is_open() && second.is_open());
  TFL_CHECK(destination_focus);
  tests::require(second.close());
  destination.reset();
  TFL_CHECK(!bar.alive() && !first_action.alive() && !second_action.alive());

  const auto timer = tests::require(context.create(context.root()));
  std::vector<Clock::time_point> updates;
  tests::require(context.set_updater(timer, [&] {
    updates.push_back(context.time());
    return base::Status::success();
  }));
  frame(context);
  updates.clear();
  tests::require(context.request_update_at(timer, t0 + 10ms));
  TFL_CHECK(!context.needs_frame(t0 + 9ms) && context.needs_frame(t0 + 10ms));
  frame(context, t0 + 9ms);
  TFL_CHECK(updates.empty());
  frame(context, t0 + 10ms);
  TFL_CHECK(updates == std::vector<Clock::time_point>{t0 + 10ms});
  TFL_CHECK(!context.next_update() && !context.needs_frame(t0 + 20ms));
  tests::require(context.request_update_at(timer, t0 + 30ms));
  tests::require(context.destroy(timer));
  TFL_CHECK(!context.next_update());

  const auto filter = tests::require(context.create(context.root()));
  const auto target = tests::require(context.create(context.root()));
  place(context, target, {0, 0, 50, 50});
  int filtered = 0;
  int routed = 0;
  tests::require(context.set_input_filter(filter, [&](Context &, RoutedEvent &event) {
    if (event.input<PointerEvent>()) {
      ++filtered;
    }
  }));
  tests::require(context.set_event_handler(target, [&](Context &, RoutedEvent &event) {
    if (event.input<PointerEvent>()) {
      ++routed;
      event.stop_propagation();
    }
  }));
  frame(context);
  tests::require(context.handle(PointerEvent{.position = {10, 10}}, t0));
  TFL_CHECK(filtered == 1 && routed == 1);
  tests::require(context.set_visible(filter, false));
  tests::require(context.handle(PointerEvent{.position = {10, 10}}, t0));
  TFL_CHECK(filtered == 1 && routed == 2);
  tests::require(context.set_visible(filter, true));
  tests::require(context.set_input_filter(filter, [&](Context &, RoutedEvent &event) {
    if (event.input<PointerEvent>()) {
      ++filtered;
      event.prevent_default();
      event.stop_propagation();
    }
  }));
  tests::require(context.handle(PointerEvent{.position = {10, 10}}, t0));
  TFL_CHECK(filtered == 2 && routed == 2);
  tests::require(context.destroy(filter));
  tests::require(context.handle(PointerEvent{.position = {10, 10}}, t0));
  TFL_CHECK(filtered == 2 && routed == 3);
  tests::require(context.set_visible(target, false));
  frame(context);
  TFL_CHECK(context.hit_test({10, 10}) != target);
  tests::require(context.set_visible(target, true));
  frame(context);
  TFL_CHECK(context.hit_test({10, 10}) == target);
  return 0;
}

int text_and_theme() {
#if TFL_TEST_GUI_TEXT
  auto backend = tests::make_null_context();
  auto engine = tests::require(TextEngine::create(backend.device, {{64, 64}}));
  const auto font = tests::require(engine.load_font(tests::gui_test_font(), 7));
  Context context;
  tests::require(context.set_viewport({400, 300}));
  const MenuTextStyle text{&engine, font, {1}};
  auto bar = tests::require(MenuBar::create(context, context.root(), text));
  auto menu = tests::require(bar.add_menu("A"));
  auto item = tests::require(
      menu.add_item({.text = "A", .checked = true, .shortcut = "B", .activated = [] {}}));
  frame(context);
  tests::require(menu.open());
  frame(context);
  TFL_CHECK(context.semantics(item.element()).value().label == "A");
  tests::require(item.set_text("B"));
  tests::require(item.set_shortcut("A"));
  frame(context);
  TFL_CHECK(context.semantics(item.element()).value().label == "B");
  tests::require(item.set_enabled(false));
  frame(context);
  TFL_CHECK(menu.is_open() && context.focused() == menu.element());
  const auto children = context.children(item.element());
  TFL_CHECK(context.property(children[0], Property::text_color).value() ==
            context.theme_value("gui.text_disabled").value());
  tests::require(context.set_theme_value("gui.text_disabled", Color{1, 0, 0, 1}));
  const auto changed = context.prepare_frame(t0);
  TFL_CHECK(changed.status.ok() && changed.layout.measured == 0 &&
            changed.shapes.regenerated > 0);
#endif
  return 0;
}

} // namespace

int main() {
  if (keyboard_and_composition() || pointer_and_delays() || placement_and_overflow() ||
      lifetime_and_contracts() || dynamic_entries() || surfaces_and_host_mechanisms()) {
    return 1;
  }
  return text_and_theme();
}

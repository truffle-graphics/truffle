#include "menu_internal.hpp"

#include <algorithm>
#include <utility>

namespace truffle::gui::detail {
namespace {

using namespace std::chrono_literals;
constexpr auto hover_delay = 250ms;
constexpr auto travel_delay = 300ms;

struct Transition {
  bool &changing;
  bool previous;
  explicit Transition(bool &value)
      : changing(value), previous(std::exchange(value, true)) {}
  ~Transition() { changing = previous; }
};

bool contains_point(Rect rect, Point point) {
  return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
         point.y < rect.y + rect.height;
}

float cross(Point a, Point b, Point point) {
  return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
}

} // namespace

base::Result<std::shared_ptr<MenuSession>> MenuSession::get(Context &context,
                                                            ElementId element) {
  auto root = element;
  while (const auto *node = context.find(root)) {
    if (!node->parent.valid()) {
      break;
    }
    root = node->parent;
  }
  if (!context.alive(root)) {
    return invalid_menu();
  }
  for (const auto &session : context.menu_sessions_) {
    if (session->context == &context && session->root == root) {
      return session;
    }
  }
  const auto made = context.create(root);
  if (!made.ok()) {
    return made.status();
  }
  auto session = std::make_shared<MenuSession>();
  session->context = &context;
  session->root = root;
  session->owner = made.value();
  const std::weak_ptr<MenuSession> weak = session;
  auto status = context.set_placement(session->owner, Rect{});
  if (status.ok()) {
    status = context.set_interaction(session->owner, {.hit_test = false});
  }
  if (status.ok()) {
    status = context.set_updater(session->owner, [weak] {
      const auto current = weak.lock();
      return current && current->context ? current->update() : base::Status::success();
    });
  }
  if (status.ok()) {
    status =
        context.set_input_filter(session->owner, [weak](Context &, RoutedEvent &event) {
          if (const auto current = weak.lock(); current && current->context) {
            event.status = current->filter(event);
          }
        });
  }
  if (status.ok()) {
    status = context.set_cleanup(session->owner, [weak] {
      if (const auto current = weak.lock()) {
        current->shutdown();
      }
    });
  }
  if (!status.ok()) {
    const auto removed = context.destroy(session->owner);
    return removed.ok() ? status : removed;
  }
  context.menu_sessions_.push_back(session);
  return session;
}

base::Status MenuSession::sync(const std::shared_ptr<MenuButtonState> &trigger) {
  if (!trigger->live()) {
    return invalid_menu();
  }
  auto current = get(*trigger->session->context, trigger->element);
  if (!current.ok()) {
    return current.status();
  }
  if (current.value() == trigger->session) {
    return base::Status::success();
  }
  const auto previous = trigger->session;
  if (const auto active = previous->button.lock();
      active == trigger && !previous->chain.empty()) {
    const auto status = previous->run(
        previous->owner, [previous] { return previous->close_from(0, false); });
    if (!status.ok()) {
      return status;
    }
  }
  current.value()->adopt(trigger->menu);
  return base::Status::success();
}

base::Status MenuSession::sync(const std::shared_ptr<MenuState> &menu) {
  if (!menu || !menu->live()) {
    return invalid_menu();
  }
  auto root_menu = menu;
  while (const auto item = root_menu->parent_item.lock()) {
    const auto parent = item->parent.lock();
    if (!parent || !parent->live()) {
      return invalid_menu();
    }
    root_menu = parent;
  }
  if (const auto trigger = root_menu->button.lock()) {
    return sync(trigger);
  }
  const auto current = get(*root_menu->session->context, root_menu->element);
  if (!current.ok()) {
    return current.status();
  }
  if (current.value() != root_menu->session) {
    current.value()->adopt(root_menu);
  }
  return base::Status::success();
}

void MenuSession::adopt(const std::shared_ptr<MenuBarState> &bar) {
  if (bar->session.get() == this) {
    return;
  }
  std::erase_if(bar->session->bars,
                [&](const auto &weak) { return weak.expired() || weak.lock() == bar; });
  bar->session = shared_from_this();
  bars.push_back(bar);
}

void MenuSession::adopt(const std::shared_ptr<MenuState> &root_menu) {
  std::vector<std::shared_ptr<MenuState>> subtrees{root_menu};
  while (!subtrees.empty()) {
    const auto menu = subtrees.back();
    subtrees.pop_back();
    if (menu->session.get() != this) {
      std::erase_if(menu->session->menus, [&](const auto &weak) {
        return weak.expired() || weak.lock() == menu;
      });
      menu->session = shared_from_this();
      menus.push_back(menu);
    }
    if (const auto trigger = menu->button.lock()) {
      trigger->session = shared_from_this();
      if (const auto bar = trigger->bar.lock()) {
        adopt(bar);
      }
    }
    for (const auto &item : menu->items) {
      item->session = shared_from_this();
      if (item->submenu && item->submenu->live()) {
        subtrees.push_back(item->submenu);
      }
    }
  }
}

base::Status MenuSession::run(ElementId element, const Context::Updater &update) {
  if (context == nullptr || !context->alive(element)) {
    return invalid_menu();
  }
  return context->run_element_update(element, update);
}

std::optional<std::size_t>
MenuSession::index(const std::shared_ptr<MenuState> &menu) const {
  const auto found = std::find(chain.begin(), chain.end(), menu);
  return found == chain.end() ? std::nullopt
                              : std::optional<std::size_t>{
                                    static_cast<std::size_t>(found - chain.begin())};
}

bool MenuSession::contains(ElementId element) const {
  if (!context) {
    return false;
  }
  for (const auto &menu : chain) {
    if (menu_descendant(*context, element, menu->element)) {
      return true;
    }
  }
  const auto trigger = button.lock();
  return trigger && trigger->live() &&
         menu_descendant(*context, element, trigger->element);
}

base::Status MenuSession::cancel_pending() {
  pending.reset();
  return context ? context->request_update_at(owner, std::nullopt) : invalid_menu();
}

base::Status MenuSession::cancel_pending(const std::shared_ptr<MenuItemState> &item) {
  return pending && pending->item.lock() == item ? cancel_pending()
                                                 : base::Status::success();
}

base::Status MenuSession::close_from(std::size_t start, bool restore) {
  if (!context) {
    return invalid_menu();
  }
  if (start >= chain.size()) {
    return base::Status::success();
  }
  Transition transition(changing);
  const auto trigger = button.lock();
  const ElementId previous_focus = restore_focus;
  const auto first_closed = chain[start];
  std::vector<std::shared_ptr<MenuState>> closed(
      chain.begin() + static_cast<std::ptrdiff_t>(start), chain.end());
  chain.erase(chain.begin() + static_cast<std::ptrdiff_t>(start), chain.end());
  for (const auto &menu : closed) {
    menu->open = false;
  }
  const auto pressed_item = pressed.lock();
  pressed.reset();
  pointer.reset();
  auto status = cancel_pending();
  if (!status.ok()) {
    return status;
  }
  if (pressed_item && pressed_item->live()) {
    status = pressed_item->appearance();
    if (!status.ok()) {
      return status;
    }
  }
  for (auto menu = closed.rbegin(); menu != closed.rend(); ++menu) {
    if ((*menu)->live() && context->visible((*menu)->element)) {
      status = context->set_visible((*menu)->element, false);
      if (!status.ok()) {
        return status;
      }
    }
  }
  if (const auto item = first_closed->parent_item.lock(); item && item->live()) {
    status = item->appearance();
    if (!status.ok()) {
      return status;
    }
  }
  if (chain.empty()) {
    button.reset();
    restore_focus = {};
    aim_origin.reset();
    previous_pointer.reset();
    if (trigger && trigger->live()) {
      status = trigger->appearance();
      if (!status.ok()) {
        return status;
      }
    }
    if (restore && context->input().focused) {
      ElementId candidate = previous_focus;
      const auto interaction = context->interaction(candidate);
      if (!menu_input_enabled(*context, candidate) || !interaction.ok() ||
          !interaction.value().focusable) {
        candidate =
            trigger && trigger->live() && menu_input_enabled(*context, trigger->element)
                ? trigger->element
                : ElementId{};
      }
      return context->focus(candidate);
    }
  } else if (restore) {
    const auto item = first_closed->parent_item.lock();
    return item && item->eligible() && menu_input_enabled(*context, item->element)
               ? select(item)
               : context->focus(chain.back()->element);
  }
  return base::Status::success();
}

base::Status MenuSession::open_root(const std::shared_ptr<MenuButtonState> &trigger,
                                    bool last) {
  if (!trigger->live() || !trigger->menu->live() || !context->input().focused ||
      !menu_input_enabled(*context, trigger->element)) {
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI menu trigger is not eligible for opening");
  }
  if (!chain.empty() && chain.front() == trigger->menu) {
    return base::Status::success();
  }
  Transition transition(changing);
  const ElementId previous = chain.empty() ? context->focused() : restore_focus;
  auto status = close_from(0, false);
  if (!status.ok()) {
    return status;
  }
  restore_focus = previous;
  button = trigger;
  chain.push_back(trigger->menu);
  trigger->menu->open = true;
  status = context->set_visible(trigger->menu->element, true);
  if (status.ok()) {
    status = trigger->appearance();
  }
  if (status.ok()) {
    status = traverse(trigger->menu, last ? -1 : 1, true);
  }
  if (!status.ok()) {
    const auto closed = close_from(0, false);
    return closed.ok() ? status : closed;
  }
  return base::Status::success();
}

base::Status MenuSession::open_submenu(const std::shared_ptr<MenuItemState> &item) {
  const auto parent = item->parent.lock();
  const auto parent_index = index(parent);
  if (!parent_index || !item->eligible() || !item->submenu || !item->submenu->live() ||
      !menu_input_enabled(*context, item->element)) {
    return base::Status::failure(base::StatusCode::invalid_state,
                                 "GUI submenu needs an enabled item in the open chain");
  }
  if (chain.size() > *parent_index + 1 && chain[*parent_index + 1] == item->submenu) {
    return traverse(item->submenu, 1, true);
  }
  Transition transition(changing);
  auto status = close_from(*parent_index + 1, false);
  if (!status.ok()) {
    return status;
  }
  chain.push_back(item->submenu);
  item->submenu->open = true;
  status = context->set_visible(item->submenu->element, true);
  if (status.ok()) {
    status = item->appearance();
  }
  if (status.ok()) {
    status = traverse(item->submenu, 1, true);
  }
  if (!status.ok()) {
    const auto closed = close_from(*parent_index + 1);
    return closed.ok() ? status : closed;
  }
  aim_origin = context->input().pointer;
  aim_until = context->time() + travel_delay;
  return base::Status::success();
}

base::Status MenuSession::select(const std::shared_ptr<MenuItemState> &item) {
  if (!item->eligible() || !menu_input_enabled(*context, item->element)) {
    return invalid_menu();
  }
  auto status = context->focus(item->element);
  if (status.ok()) {
    status = item->appearance();
  }
  if (status.ok()) {
    status = context->request_update(owner);
  }
  return status;
}

base::Status MenuSession::activate(const std::shared_ptr<MenuItemState> &item) {
  if (!item->eligible() || !menu_input_enabled(*context, item->element)) {
    return invalid_menu();
  }
  if (item->submenu && item->submenu->live()) {
    return open_submenu(item);
  }
  const auto action = item->desc.activated;
  const auto toggled = item->desc.toggled;
  std::optional<bool> checked;
  if (item->desc.toggles) {
    checked = !item->desc.checked.value_or(false);
    item->desc.checked = checked;
    if (auto status = item->appearance(); !status.ok()) {
      return status;
    }
  }
  const auto status = close_from(0);
  if (!status.ok()) {
    return status;
  }
  if (checked && toggled) {
    toggled(*checked);
  }
  action();
  return base::Status::success();
}

base::Status MenuSession::traverse(const std::shared_ptr<MenuState> &menu, int direction,
                                   bool edge) {
  if (!menu || !menu->live() || !menu->open) {
    return invalid_menu();
  }
  const auto parent_index = index(menu);
  if (!parent_index) {
    return invalid_menu();
  }
  auto status = close_from(*parent_index + 1, false);
  if (!status.ok()) {
    return status;
  }
  const auto count = menu->items.size();
  std::size_t start = direction > 0 ? count - (count != 0 ? 1U : 0U) : 0;
  if (!edge) {
    const auto found =
        std::find_if(menu->items.begin(), menu->items.end(), [&](const auto &item) {
          return item->element == context->focused();
        });
    if (found != menu->items.end()) {
      start = static_cast<std::size_t>(found - menu->items.begin());
    }
  }
  for (std::size_t step = 0; step < count; ++step) {
    start = direction > 0 ? (start + 1) % count : (start + count - 1) % count;
    if (menu->items[start]->eligible() &&
        menu_input_enabled(*context, menu->items[start]->element)) {
      return select(menu->items[start]);
    }
  }
  return context->focus(menu->element);
}

base::Status MenuSession::traverse_bar(const std::shared_ptr<MenuButtonState> &from,
                                       int direction, bool open) {
  const auto bar = from->bar.lock();
  if (!bar || !bar->live()) {
    return base::Status::success();
  }
  std::vector<std::shared_ptr<MenuButtonState>> buttons;
  for (const auto &weak : bar->buttons) {
    if (auto current = weak.lock();
        current && current->live() && menu_input_enabled(*context, current->element)) {
      buttons.push_back(current);
    }
  }
  if (buttons.empty()) {
    return base::Status::success();
  }
  const auto found = std::find(buttons.begin(), buttons.end(), from);
  std::size_t position =
      found == buttons.end() ? 0 : static_cast<std::size_t>(found - buttons.begin());
  position = direction > 0 ? (position + 1) % buttons.size()
                           : (position + buttons.size() - 1) % buttons.size();
  auto status = sync(buttons[position]);
  if (!status.ok()) {
    return status;
  }
  return open ? buttons[position]->session->open_root(buttons[position])
              : context->focus(buttons[position]->element);
}

base::Status MenuSession::focus_changed() {
  if (!context || changing || chain.empty() || contains(context->focused())) {
    return base::Status::success();
  }
  return close_from(0, false);
}

std::shared_ptr<MenuItemState> MenuSession::item_at(Point position,
                                                    ElementId target) const {
  for (auto menu = chain.rbegin(); menu != chain.rend(); ++menu) {
    if (!menu_descendant(*context, target, (*menu)->element)) {
      continue;
    }
    for (const auto &item : (*menu)->items) {
      if (!item->live() || item->separator) {
        continue;
      }
      const auto rect = context->absolute_bounds(item->element);
      if (rect.ok() && contains_point(rect.value(), position)) {
        return item;
      }
    }
    return {};
  }
  return {};
}

bool MenuSession::aiming(Point position, std::size_t parent) const {
  if (!aim_origin || !previous_pointer || parent + 1 >= chain.size() ||
      context->time() >= aim_until) {
    return false;
  }
  const auto child = context->absolute_bounds(chain[parent + 1]->element);
  if (!child.ok()) {
    return false;
  }
  const Rect rect = child.value();
  const float edge = rect.x >= aim_origin->x ? rect.x : rect.x + rect.width;
  const bool right = edge >= aim_origin->x;
  if ((right && (position.x < previous_pointer->x || position.x < aim_origin->x ||
                 position.x > edge)) ||
      (!right && (position.x > previous_pointer->x || position.x > aim_origin->x ||
                  position.x < edge))) {
    return false;
  }
  const Point top{edge, rect.y - 6};
  const Point bottom{edge, rect.y + rect.height + 6};
  const float a = cross(*aim_origin, top, position);
  const float b = cross(top, bottom, position);
  const float c = cross(bottom, *aim_origin, position);
  return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}

base::Status MenuSession::schedule(const std::shared_ptr<MenuItemState> &item,
                                   Context::Clock::time_point due, bool extend) {
  if (pending && pending->item.lock() == item && (!extend || due <= pending->due)) {
    return base::Status::success();
  }
  pending = Pending{item, due};
  return context->request_update_at(owner, due);
}

base::Status MenuSession::hover(Point position, ElementId target) {
  const auto trigger = button.lock();
  const auto bar = trigger ? trigger->bar.lock() : nullptr;
  const bool in_popup = std::ranges::any_of(chain, [&](const auto &menu) {
    return menu_descendant(*context, target, menu->element);
  });
  if (bar && bar->live() && !in_popup) {
    for (const auto &weak : bar->buttons) {
      const auto candidate = weak.lock();
      if (candidate && candidate->live() &&
          menu_input_enabled(*context, candidate->element) &&
          menu_descendant(*context, target, candidate->element)) {
        auto status = cancel_pending();
        return status.ok() ? open_root(candidate) : status;
      }
    }
  }
  const auto item = item_at(position, target);
  if (!item || !item->eligible()) {
    return cancel_pending();
  }
  const auto parent = index(item->parent.lock());
  if (!parent) {
    return cancel_pending();
  }
  if (*parent + 1 < chain.size()) {
    if (chain[*parent + 1] == item->submenu) {
      aim_origin = position;
      aim_until = context->time() + travel_delay;
      auto status = cancel_pending();
      return status.ok() ? select(item) : status;
    }
    const bool protected_travel = aiming(position, *parent);
    return schedule(item, protected_travel ? aim_until : context->time() + hover_delay,
                    protected_travel);
  }
  auto status = select(item);
  if (!status.ok()) {
    return status;
  }
  return item->submenu && item->submenu->live()
             ? schedule(item, context->time() + hover_delay)
             : cancel_pending();
}

base::Status MenuSession::key(const KeyEvent &input, RoutedEvent &event) {
  if (!input.down || chain.empty()) {
    return base::Status::success();
  }
  auto menu = chain.front();
  for (auto candidate = chain.rbegin(); candidate != chain.rend(); ++candidate) {
    if (menu_descendant(*context, context->focused(), (*candidate)->element)) {
      menu = *candidate;
      break;
    }
  }
  const auto current =
      std::find_if(menu->items.begin(), menu->items.end(),
                   [&](const auto &item) { return item->element == context->focused(); });
  const auto item = current == menu->items.end() ? nullptr : *current;
  base::Status status;
  switch (input.key) {
  case Key::down:
    status = traverse(menu, 1);
    break;
  case Key::up:
    status = traverse(menu, -1);
    break;
  case Key::home:
    status = traverse(menu, 1, true);
    break;
  case Key::end:
    status = traverse(menu, -1, true);
    break;
  case Key::right:
    if (item && item->submenu && item->eligible()) {
      status = open_submenu(item);
    } else if (menu == chain.front()) {
      if (const auto trigger = button.lock()) {
        status = traverse_bar(trigger, 1, true);
      }
    }
    break;
  case Key::left:
    if (const auto position = index(menu); position && *position > 0) {
      status = close_from(*position);
    } else {
      status = close_from(1, false);
      if (status.ok()) {
        if (const auto trigger = button.lock()) {
          status = traverse_bar(trigger, -1, true);
        }
      }
    }
    break;
  case Key::escape:
    status = close_from(chain.size() - 1);
    break;
  case Key::enter:
  case Key::space:
    if (!input.repeat && item && item->eligible()) {
      status = activate(item);
    }
    break;
  case Key::tab:
    status = close_from(0);
    if (status.ok()) {
      status = context->focus_next(has(input.modifiers, Modifiers::shift));
    }
    break;
  default:
    return base::Status::success();
  }
  event.prevent_default();
  event.stop_propagation();
  if (status.ok() && !chain.empty()) {
    status = cancel_pending();
  }
  return status;
}

base::Status MenuSession::filter(RoutedEvent &event) {
  if (chain.empty()) {
    return base::Status::success();
  }
  const auto trigger = button.lock();
  if (!trigger || !trigger->live() || !menu_input_enabled(*context, trigger->element)) {
    return close_from(0, false);
  }
  if (const auto *focus = event.input<FocusEvent>(); focus && !focus->focused) {
    return close_from(0, false);
  }
  if (const auto *input = event.input<KeyEvent>()) {
    return key(*input, event);
  }
  const auto *input = event.input<PointerEvent>();
  if (!input) {
    return base::Status::success();
  }
  if (input->action == PointerAction::cancel) {
    return close_from(0, false);
  }
  if (input->action == PointerAction::leave) {
    previous_pointer.reset();
    return cancel_pending();
  }
  if (input->action == PointerAction::move) {
    const auto status = hover(input->position, event.target);
    previous_pointer = input->position;
    return status;
  }
  const bool in_popup = std::ranges::any_of(chain, [&](const auto &menu) {
    return menu_descendant(*context, event.target, menu->element);
  });
  if (!in_popup) {
    if (input->action == PointerAction::down && !contains(event.target)) {
      return close_from(0, false); // the same outside click still reaches its target
    }
    if (input->action == PointerAction::up && pointer == input->pointer) {
      const auto down = pressed.lock();
      pressed.reset();
      pointer.reset();
      if (down && down->live()) {
        return down->appearance();
      }
    }
    return base::Status::success();
  }
  event.prevent_default();
  event.stop_propagation();
  if (input->button != PointerButton::primary) {
    return base::Status::success();
  }
  const auto item = item_at(input->position, event.target);
  if (input->action == PointerAction::down) {
    auto status = cancel_pending();
    if (!status.ok()) {
      return status;
    }
    pressed.reset();
    pointer = input->pointer;
    if (item && item->eligible()) {
      pressed = item;
      return select(item);
    }
  } else if (input->action == PointerAction::up && pointer == input->pointer) {
    const auto down = pressed.lock();
    pressed.reset();
    pointer.reset();
    if (down && down->live()) {
      const auto status = down->appearance();
      if (!status.ok()) {
        return status;
      }
    }
    if (down && down == item && down->eligible()) {
      return activate(down);
    }
  }
  return base::Status::success();
}

base::Status MenuSession::update() {
  if (!context || chain.empty()) {
    return base::Status::success();
  }
  if (pending && pending->due <= context->time()) {
    const auto item = pending->item.lock();
    auto status = cancel_pending();
    if (!status.ok()) {
      return status;
    }
    const auto point = context->input().pointer;
    if (item && item->eligible() && point &&
        item_at(*point, context->hit_test(*point)) == item) {
      const auto parent = index(item->parent.lock());
      if (parent) {
        status = close_from(*parent + 1, false);
        if (status.ok()) {
          status = select(item);
        }
        if (status.ok() && item->submenu && item->submenu->live()) {
          status = open_submenu(item);
        }
        if (!status.ok()) {
          return status;
        }
      }
    }
  }
  for (auto menu = chain.rbegin(); menu != chain.rend(); ++menu) {
    const auto focused = context->focused();
    if (!menu_descendant(*context, focused, (*menu)->element)) {
      continue;
    }
    const auto outer = context->absolute_bounds((*menu)->element);
    const auto row = context->absolute_bounds(focused);
    const auto params = context->layout_params((*menu)->element);
    if (!outer.ok()) {
      return outer.status();
    }
    if (!row.ok()) {
      return row.status();
    }
    if (!params.ok()) {
      return params.status();
    }
    const float top = outer.value().y + params.value().padding.top;
    const float bottom =
        outer.value().y + outer.value().height - params.value().padding.bottom;
    auto offset = context->child_offset((*menu)->element);
    if (row.value().y < top) {
      offset.y += top - row.value().y;
    } else if (row.value().y + row.value().height > bottom) {
      offset.y -= row.value().y + row.value().height - bottom;
    }
    float extent = 0;
    for (const auto &item : (*menu)->items) {
      if (!item->live()) {
        continue;
      }
      const auto bounds = context->bounds(item->element);
      if (!bounds.ok()) {
        return bounds.status();
      }
      extent = std::max(extent, bounds.value().y + bounds.value().height);
    }
    offset.y = std::clamp(
        offset.y,
        -std::max(0.0F, extent + params.value().padding.bottom - outer.value().height),
        0.0F);
    return context->set_child_offset((*menu)->element, offset);
  }
  return base::Status::success();
}

void MenuSession::retire(const std::shared_ptr<MenuState> &menu) {
  if (context) {
    if (const auto position = index(menu)) {
      const auto status = close_from(*position, false);
      if (!status.ok()) {
        context->event_status_ = status;
      }
    }
  }
  menu->retired = true;
  menu->open = false;
  std::erase_if(menus,
                [&](const auto &weak) { return weak.expired() || weak.lock() == menu; });
  if (const auto trigger = menu->button.lock();
      trigger && trigger->live() && context &&
      menu_descendant(*context, trigger->element, root)) {
    auto status = context->set_interaction(trigger->element, {.enabled = false});
    if (status.ok()) {
      status = trigger->appearance();
    }
    if (!status.ok()) {
      context->event_status_ = status;
    }
  }
  if (const auto item = menu->parent_item.lock(); item && item->live()) {
    item->submenu.reset();
    if (context && menu_descendant(*context, item->element, root)) {
      auto status = cancel_pending(item);
      if (status.ok() && context->focused() == item->element) {
        if (const auto parent = item->parent.lock();
            parent && parent->live() && parent->open) {
          status = traverse(parent, 1);
        }
      }
      if (status.ok()) {
        status = item->interaction();
      }
      if (status.ok()) {
        status = item->appearance();
      }
      if (!status.ok()) {
        context->event_status_ = status;
      }
    }
  }
}

void MenuSession::retire(const std::shared_ptr<MenuItemState> &item) {
  const bool focused =
      context && menu_descendant(*context, context->focused(), item->element);
  if (context) {
    const auto status = cancel_pending(item);
    if (!status.ok()) {
      context->event_status_ = status;
    }
  }
  item->retired = true;
  if (item->submenu) {
    retire(item->submenu);
  }
  if (const auto parent = item->parent.lock()) {
    if (focused && context && parent->live() && parent->open &&
        menu_descendant(*context, parent->element, root)) {
      const auto status = traverse(parent, 1);
      if (!status.ok()) {
        context->event_status_ = status;
      }
    }
    std::erase(parent->items, item);
  }
}

void MenuSession::retire(const std::shared_ptr<MenuButtonState> &trigger) {
  trigger->retired = true;
  if (context && button.lock() == trigger && !chain.empty()) {
    const auto status = close_from(0, false);
    if (!status.ok()) {
      context->event_status_ = status;
    }
  }
}

void MenuSession::retire(const std::shared_ptr<MenuBarState> &bar) {
  bar->retired = true;
  std::erase_if(bars,
                [&](const auto &weak) { return weak.expired() || weak.lock() == bar; });
}

void MenuSession::shutdown() {
  if (context && !chain.empty()) {
    const auto status = close_from(0, false);
    if (!status.ok()) {
      context->event_status_ = status;
    }
  }
  if (context && !context->alive(root)) {
    const auto destination = [&](ElementId element) {
      while (const auto *node = context->find(element)) {
        if (!node->parent.valid()) {
          return element;
        }
        element = node->parent;
      }
      return ElementId{};
    };
    const auto moved_menus = menus;
    for (const auto &weak : moved_menus) {
      const auto menu = weak.lock();
      if (!menu || !menu->live() || menu->session.get() != this ||
          !destination(menu->element).valid()) {
        continue;
      }
      const auto next = get(*context, menu->element);
      if (next.ok()) {
        next.value()->adopt(menu);
      } else {
        context->event_status_ = next.status();
      }
    }
    const auto moved_bars = bars;
    for (const auto &weak : moved_bars) {
      const auto bar = weak.lock();
      if (!bar || !bar->live() || bar->session.get() != this ||
          !destination(bar->element).valid()) {
        continue;
      }
      const auto next = get(*context, bar->element);
      if (next.ok()) {
        next.value()->adopt(bar);
      } else {
        context->event_status_ = next.status();
      }
    }
  }
  if (context) {
    std::erase_if(context->menu_sessions_,
                  [this](const auto &session) { return session.get() == this; });
  }
  context = nullptr;
  for (const auto &menu : chain) {
    menu->open = false;
  }
  chain.clear();
  button.reset();
  pending.reset();
  pressed.reset();
  pointer.reset();
  menus.clear();
  bars.clear();
}

} // namespace truffle::gui::detail

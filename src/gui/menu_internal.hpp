#pragma once

#include "truffle/gui/menus.hpp"
#include "truffle/gui/widgets.hpp"

#include <chrono>
#include <vector>

namespace truffle::gui::detail {

base::Status invalid_menu();
bool menu_input_enabled(const Context &context, ElementId element);
bool menu_descendant(const Context &context, ElementId element, ElementId ancestor);

struct MenuState {
  std::shared_ptr<MenuSession> session;
  ElementId element;
  MenuTextStyle text;
  std::vector<std::shared_ptr<MenuItemState>> items;
  std::weak_ptr<MenuButtonState> button;
  std::weak_ptr<MenuItemState> parent_item;
  bool retired = false;
  bool open = false;

  bool live() const;
};

struct MenuItemState {
  std::shared_ptr<MenuSession> session;
  std::weak_ptr<MenuState> parent;
  ElementId element;
  ElementId mark;
  ElementId arrow;
  Widget label;
  Widget shortcut;
  MenuItemDesc desc;
  std::shared_ptr<MenuState> submenu;
  bool separator = false;
  bool retired = false;
  bool painted_check = false;
  bool painted_arrow = false;

  bool live() const;
  bool eligible() const;
  base::Status appearance();
  base::Status interaction();
  base::Status event(RoutedEvent &event);
};

struct MenuButtonState : std::enable_shared_from_this<MenuButtonState> {
  std::shared_ptr<MenuSession> session;
  std::shared_ptr<MenuState> menu;
  std::weak_ptr<MenuBarState> bar;
  ElementId element;
  ElementId arrow;
  Widget label;
  bool retired = false;
  bool hover = false;
  bool space_pressed = false;
  std::optional<std::uint32_t> pointer;

  bool live() const;
  base::Status appearance();
  base::Status event(RoutedEvent &event);
};

struct MenuBarState {
  std::shared_ptr<MenuSession> session;
  ElementId element;
  MenuTextStyle text;
  std::vector<std::weak_ptr<MenuButtonState>> buttons;
  bool retired = false;

  bool live() const;
};

// One chain per host surface. Tree identities own callbacks and timers; model
// references never keep a Context alive.
struct MenuSession : std::enable_shared_from_this<MenuSession> {
  Context *context = nullptr;
  ElementId root;
  ElementId owner;
  std::vector<std::weak_ptr<MenuState>> menus;
  std::vector<std::weak_ptr<MenuBarState>> bars;
  std::vector<std::shared_ptr<MenuState>> chain;
  std::weak_ptr<MenuButtonState> button;
  ElementId restore_focus;
  bool changing = false;
  struct Pending {
    std::weak_ptr<MenuItemState> item;
    Context::Clock::time_point due;
  };
  std::optional<Pending> pending;
  std::weak_ptr<MenuItemState> pressed;
  std::optional<std::uint32_t> pointer;
  std::optional<Point> previous_pointer;
  std::optional<Point> aim_origin;
  Context::Clock::time_point aim_until{};

  static base::Result<std::shared_ptr<MenuSession>> get(Context &context,
                                                        ElementId element);
  static base::Status sync(const std::shared_ptr<MenuButtonState> &button);
  static base::Status sync(const std::shared_ptr<MenuState> &menu);
  base::Status run(ElementId element, const Context::Updater &update);
  base::Status open_root(const std::shared_ptr<MenuButtonState> &trigger,
                         bool last = false);
  base::Status open_submenu(const std::shared_ptr<MenuItemState> &item);
  base::Status close_from(std::size_t index, bool restore = true);
  base::Status select(const std::shared_ptr<MenuItemState> &item);
  base::Status activate(const std::shared_ptr<MenuItemState> &item);
  base::Status traverse(const std::shared_ptr<MenuState> &menu, int direction,
                        bool edge = false);
  base::Status traverse_bar(const std::shared_ptr<MenuButtonState> &from, int direction,
                            bool open);
  base::Status filter(RoutedEvent &event);
  base::Status key(const KeyEvent &key, RoutedEvent &event);
  base::Status update();
  base::Status cancel_pending();
  base::Status cancel_pending(const std::shared_ptr<MenuItemState> &item);
  base::Status focus_changed();
  void retire(const std::shared_ptr<MenuState> &menu);
  void retire(const std::shared_ptr<MenuItemState> &item);
  void retire(const std::shared_ptr<MenuButtonState> &trigger);
  void retire(const std::shared_ptr<MenuBarState> &bar);
  void shutdown();
  std::optional<std::size_t> index(const std::shared_ptr<MenuState> &menu) const;
  bool contains(ElementId element) const;

private:
  void adopt(const std::shared_ptr<MenuState> &menu);
  void adopt(const std::shared_ptr<MenuBarState> &bar);
  base::Status hover(Point position, ElementId target);
  base::Status schedule(const std::shared_ptr<MenuItemState> &item,
                        Context::Clock::time_point due, bool extend = false);
  bool aiming(Point position, std::size_t parent) const;
  std::shared_ptr<MenuItemState> item_at(Point position, ElementId target) const;
};

} // namespace truffle::gui::detail

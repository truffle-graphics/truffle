#pragma once

#include "truffle/gui/context.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace truffle::gui {
namespace detail {
struct MenuState;
struct MenuItemState;
struct MenuButtonState;
struct MenuBarState;
} // namespace detail

struct MenuTextStyle {
  TextEngine *text_engine = nullptr;
  FontId font;
  TextureId atlas;
};

struct MenuItemDesc {
  std::string text;
  bool enabled = true;
  std::optional<bool> checked; // empty has no checkmark; setters never invoke actions
  std::string shortcut;        // display only; the application owns shortcut bindings
  std::function<void()> activated;
};

class MenuItem;
class MenuSeparator;
class MenuButton;

// Checked, non-owning references to a Context-owned persistent menu tree.
// Context, text engine and atlas must outlive use. Attaching a menu transfers
// its element under the trigger, so destroying that trigger destroys the menu.
class Menu {
public:
  Menu() = default;
  [[nodiscard]] static base::Result<Menu> create(Context &context, ElementId parent,
                                                 MenuTextStyle text = {});
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] ElementId trigger() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] base::Result<MenuItem> add_item(MenuItemDesc desc);
  // Creates both the submenu and its arrow-bearing parent item.
  [[nodiscard]] base::Result<Menu> add_submenu(std::string text, bool enabled = true);
  [[nodiscard]] base::Result<MenuSeparator> add_separator();
  [[nodiscard]] base::Status open();
  [[nodiscard]] base::Status close();

private:
  friend class MenuItem;
  friend class MenuButton;
  friend class MenuBar;
  explicit Menu(std::shared_ptr<detail::MenuState> state) : state_(std::move(state)) {}
  std::shared_ptr<detail::MenuState> state_;
};

class MenuItem {
public:
  MenuItem() = default;
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  [[nodiscard]] base::Status set_text(std::string text);
  [[nodiscard]] base::Result<std::string> text() const;
  [[nodiscard]] base::Status set_enabled(bool enabled);
  [[nodiscard]] base::Status set_checked(std::optional<bool> checked);
  [[nodiscard]] base::Result<std::optional<bool>> checked() const;
  [[nodiscard]] base::Status set_shortcut(std::string shortcut);
  [[nodiscard]] base::Status set_action(std::function<void()> action);
  // An item has an action OR a submenu, never both. The submenu must be
  // unattached and belong to the same Context/surface. Cycles are rejected.
  [[nodiscard]] base::Status set_submenu(Menu menu);
  [[nodiscard]] base::Result<Menu> submenu() const;

private:
  friend class Menu;
  explicit MenuItem(std::shared_ptr<detail::MenuItemState> state)
      : state_(std::move(state)) {}
  std::shared_ptr<detail::MenuItemState> state_;
};

class MenuSeparator {
public:
  MenuSeparator() = default;
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;

private:
  friend class Menu;
  explicit MenuSeparator(std::shared_ptr<detail::MenuItemState> state)
      : state_(std::move(state)) {}
  std::shared_ptr<detail::MenuItemState> state_;
};

class MenuButton {
public:
  MenuButton() = default;
  // Creates/wires its menu automatically, including for a standalone dropdown.
  [[nodiscard]] static base::Result<MenuButton>
  create(Context &context, ElementId parent, std::string label, MenuTextStyle text = {});
  [[nodiscard]] static base::Result<MenuButton> create(Context &context, ElementId parent,
                                                       std::string label, Menu menu);
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  [[nodiscard]] base::Result<Menu> menu() const;
  [[nodiscard]] base::Status set_text(std::string text);
  [[nodiscard]] base::Status set_enabled(bool enabled);
  [[nodiscard]] base::Status open();
  [[nodiscard]] base::Status close();

private:
  friend class MenuBar;
  explicit MenuButton(std::shared_ptr<detail::MenuButtonState> state)
      : state_(std::move(state)) {}
  std::shared_ptr<detail::MenuButtonState> state_;
};

class MenuBar {
public:
  MenuBar() = default;
  [[nodiscard]] static base::Result<MenuBar> create(Context &context, ElementId parent,
                                                    MenuTextStyle text = {});
  [[nodiscard]] ElementId element() const noexcept;
  [[nodiscard]] bool alive() const noexcept;
  // The named menu's trigger is automatically created and connected.
  [[nodiscard]] base::Result<Menu> add_menu(std::string label);
  [[nodiscard]] base::Result<MenuButton> add_menu(std::string label, Menu menu);

private:
  explicit MenuBar(std::shared_ptr<detail::MenuBarState> state)
      : state_(std::move(state)) {}
  std::shared_ptr<detail::MenuBarState> state_;
};

} // namespace truffle::gui

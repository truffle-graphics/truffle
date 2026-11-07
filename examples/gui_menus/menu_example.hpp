#pragma once

#include "truffle/gui/menus.hpp"

#include <stdexcept>
#include <utility>

namespace truffle::examples {

// Host-independent composition; the application supplies fonts, actions and layout.
struct MenuExample {
  gui::MenuBar bar;
  gui::MenuButton dropdown;
  gui::MenuItem checked;

  MenuExample(gui::Context &context, gui::ElementId parent, gui::MenuTextStyle text,
              std::function<void()> run) {
    bar = take(gui::MenuBar::create(context, parent, text));
    auto actions = take(bar.add_menu("Actions"));
    take(actions.add_item({.text = "Run", .shortcut = "Ctrl+R", .activated = run}));
    auto more = take(actions.add_submenu("More"));
    take(more.add_item({.text = "Run again", .activated = std::move(run)}));
    dropdown = take(gui::MenuButton::create(context, parent, "Mode", text));
    auto modes = take(dropdown.menu());
    checked =
        take(modes.add_item({.text = "Design", .checked = true, .activated = [] {}}));
    take(modes.add_separator());
    take(modes.add_item({.text = "Add mode...", .enabled = false}));
  }

private:
  template <typename T> static T take(base::Result<T> result) {
    if (!result.ok()) {
      throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
  }
};

} // namespace truffle::examples

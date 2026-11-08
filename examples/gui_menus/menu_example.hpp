#pragma once

#include "truffle/gui/ui.hpp"

#include <functional>
#include <utility>

namespace truffle::examples {

// Host-independent composition; the application supplies fonts, actions and layout.
struct MenuExample {
  gui::MenuBar bar;
  gui::Menu modes;
  gui::MenuItem design;

  MenuExample(gui::Container parent, std::function<void()> run) {
    bar = parent.menu_bar();
    auto actions = bar.menu("Actions");
    actions.item("Run").shortcut("Ctrl+R").on_click(run);
    actions.menu("More").item("Run again").on_click(std::move(run));
    modes = parent.menu_button("Mode");
    design = modes.item("Design").checkable(true);
    modes.separator();
    modes.item("Add mode...").enabled(false);
  }
};

} // namespace truffle::examples

#pragma once

#include "truffle/base/status.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace truffle::gui::dock {

// `horizontal` places a split's children side by side, `vertical` stacks them.
enum class Axis : std::uint8_t { horizontal, vertical };

// Where a dragged panel lands relative to the area it is dropped on: beside
// it (splitting the area) or `center`, as another tab.
enum class DropSide : std::uint8_t { left, right, top, bottom, center };

// A dock layout: tabbed panels, or a split of two or more nodes. Panels are
// identified by application-chosen ids that are unique across the layout.
struct DockNode {
    enum class Kind : std::uint8_t { tabs, split };

    Kind kind = Kind::tabs;
    std::vector<std::string> panels; // tabs: panel ids in tab order
    std::size_t active = 0;          // tabs: the panel shown
    Axis axis = Axis::horizontal;    // split
    std::vector<float> fractions;    // split: each child's share, summing to 1
    std::vector<DockNode> children;  // split

    [[nodiscard]] static DockNode tabs(std::vector<std::string> panels, std::size_t active = 0);
    // Empty `fractions` share the space equally.
    [[nodiscard]] static DockNode split(Axis axis, std::vector<DockNode> children,
                                        std::vector<float> fractions = {});

    [[nodiscard]] bool empty() const noexcept {
        return kind == Kind::tabs && panels.empty();
    }
    friend bool operator==(const DockNode&, const DockNode&) = default;
};

// Every tabs node is non-empty with a valid active tab (only an empty root is
// allowed), splits have two or more children with matching positive
// fractions, and panel ids are non-empty and unique.
[[nodiscard]] base::Status validate(const DockNode& layout);
[[nodiscard]] bool contains(const DockNode& layout, std::string_view panel);
// Panel ids in tree order.
[[nodiscard]] std::vector<std::string> panel_ids(const DockNode& layout);

// Adds a panel that is not in the layout next to the area holding `anchor`.
// An empty layout takes the panel as its only tab; `anchor` is ignored then.
[[nodiscard]] base::Status insert_panel(DockNode& layout, std::string_view panel,
                                        std::string_view anchor, DropSide side);
// Removes a panel; emptied tabs disappear and single-child splits collapse.
[[nodiscard]] base::Status remove_panel(DockNode& layout, std::string_view panel);
// Moves a panel next to the area holding `anchor` (another panel). The layout
// is unchanged on failure.
[[nodiscard]] base::Status move_panel(DockNode& layout, std::string_view panel,
                                      std::string_view anchor, DropSide side);
// Makes `panel` the shown tab of its area.
[[nodiscard]] base::Status activate_panel(DockNode& layout, std::string_view panel);

// Small text description, for example
//   (split h 2500 7500 (tabs 0 "scene" "assets") (tabs 0 "inspector"))
// Split shares are parts per 10000; tabs list the active index, then ids.
[[nodiscard]] std::string serialize(const DockNode& layout);
[[nodiscard]] base::Result<DockNode> parse(std::string_view text);

// Named layouts, saved together as a `dock-presets 1` line followed by one
// `preset "name" (layout)` line each.
class DockPresets {
public:
    [[nodiscard]] base::Status set(std::string name, DockNode layout);
    [[nodiscard]] const DockNode* find(std::string_view name) const noexcept;
    bool erase(std::string_view name);
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] std::size_t size() const noexcept { return presets_.size(); }

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static base::Result<DockPresets> parse(std::string_view text);

private:
    std::map<std::string, DockNode, std::less<>> presets_;
};

} // namespace truffle::gui::dock

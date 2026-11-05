#pragma once

#include "truffle/gui/widgets.hpp"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace truffle::gui {

// A description of one widget and its subtree. `build` turns it into the same
// elements and widgets the object API would create; nothing is retained.
// Unset layout fields keep the widget's defaults.
struct BuildNode {
    WidgetDesc desc;
    std::shared_ptr<const Layout> layout;
    std::optional<float> width_value;
    std::optional<float> height_value;
    std::optional<float> weight_value;
    std::optional<Size> min_value;
    std::optional<Point> position_value;
    std::optional<GridCell> cell_value;
    std::optional<Edges> padding_value; // applied as a property override
    bool enabled = true;
    Widget* target = nullptr;
    std::vector<BuildNode> children;

    BuildNode& width(float value) { width_value = value; return *this; }
    BuildNode& height(float value) { height_value = value; return *this; }
    BuildNode& size(Size value) { width_value = value.width; height_value = value.height;
                                  return *this; }
    BuildNode& weight(float value) { weight_value = value; return *this; }
    BuildNode& min(Size value) { min_value = value; return *this; }
    BuildNode& at(Point value) { position_value = value; return *this; }
    BuildNode& cell(GridCell value) { cell_value = value; return *this; }
    BuildNode& padding(Edges value) { padding_value = value; return *this; }
    BuildNode& disabled() { enabled = false; return *this; }
    // Receives the created widget; `out` must outlive `build`.
    BuildNode& bind(Widget& out) { target = &out; return *this; }
    BuildNode& with_layout(std::shared_ptr<const Layout> value) {
        layout = std::move(value);
        return *this;
    }
};

using BuildChildren = std::initializer_list<BuildNode>;

namespace ui {

[[nodiscard]] BuildNode panel(BuildChildren children = {});
[[nodiscard]] BuildNode row(float spacing, BuildChildren children = {});
[[nodiscard]] BuildNode column(float spacing, BuildChildren children = {});
[[nodiscard]] BuildNode grid(GridOptions options, BuildChildren children = {});
[[nodiscard]] BuildNode scroll(BuildChildren children = {});
[[nodiscard]] BuildNode label(std::string text);
[[nodiscard]] BuildNode button(std::string text, std::function<void()> activated = {});
[[nodiscard]] BuildNode checkbox(std::string text, bool checked = false,
                                 std::function<void(bool)> toggled = {});
[[nodiscard]] BuildNode slider(float min, float max, float value,
                               std::function<void(float)> value_changed = {});
[[nodiscard]] BuildNode image(TextureId texture, Size size);
[[nodiscard]] BuildNode text_edit(std::string text, bool multiline = false,
                                  std::function<void(const std::string&)> changed = {});

} // namespace ui

// Text defaults for nodes that don't name their own engine, font or atlas.
struct BuildOptions {
    TextEngine* text_engine = nullptr;
    FontId font;
    TextureId atlas;
};

// Creates `node` under `parent` and returns the root widget. On failure the
// partially built subtree is destroyed and bound outputs are left empty.
[[nodiscard]] base::Result<Widget> build(Context& context, ElementId parent,
                                         const BuildNode& node,
                                         const BuildOptions& options = {});

} // namespace truffle::gui

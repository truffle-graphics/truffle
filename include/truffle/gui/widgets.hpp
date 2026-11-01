#pragma once

#include "truffle/gui/context.hpp"

#include <functional>
#include <memory>
#include <string>

namespace truffle::gui {

enum class WidgetKind : std::uint8_t { panel, label, image, button, checkbox, slider,
                                       scroll_area };

struct WidgetDesc {
    WidgetKind kind = WidgetKind::panel;
    std::string text; // also retained as the semantic label
    TextEngine* text_engine = nullptr;
    FontId font;
    TextureId atlas;
    TextureId image;
    Size image_size{32, 32};
    bool checked = false;
    float min = 0;
    float max = 1;
    float value = 0;
    float step = 0.01F;
    std::function<void()> activated;
    std::function<void(bool)> toggled;
    std::function<void(float)> value_changed;
};

// A checked, non-owning object-API reference. Context owns the element tree;
// dropping this reference doesn't destroy it. Context/TextEngine must outlive use.
// Programmatic setters don't invoke user-action callbacks.
class Widget {
public:
    Widget() = default;
    [[nodiscard]] static base::Result<Widget> create(Context& context, ElementId parent,
                                                     WidgetDesc desc = {});
    [[nodiscard]] ElementId element() const noexcept;
    // Scroll-area children go here; all other widgets return their own element.
    [[nodiscard]] ElementId content() const noexcept;
    [[nodiscard]] bool alive() const noexcept;
    [[nodiscard]] base::Status set_text(std::string text);
    [[nodiscard]] base::Status set_checked(bool checked);
    [[nodiscard]] base::Result<bool> checked() const;
    [[nodiscard]] base::Status set_value(float value);
    [[nodiscard]] base::Result<float> value() const;
    [[nodiscard]] base::Status set_scroll(Point offset);
    [[nodiscard]] base::Result<Point> scroll() const;
    [[nodiscard]] base::Status set_enabled(bool enabled);

private:
    struct State;
    explicit Widget(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace truffle::gui

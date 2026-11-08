#pragma once

#include "truffle/gui/text.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace truffle::gui {

enum class Property : std::uint8_t {
    fill, border_color, border_width, corner_radius, padding, spacing, font, text_color,
    min_size, font_size, selection_color, caret_color, composition_color,
    indicator_size, indicator_gap, slider_track_thickness, slider_thumb_width,
    slider_thumb_inset, menu_leading_width, menu_trailing_width, menu_column_gap, count
};

// Empty uses the size of the host-supplied font. Otherwise the size is in
// logical GUI units and must be finite and in (0, 512].
struct FontSize {
    std::optional<float> units;
    friend bool operator==(const FontSize&, const FontSize&) = default;
};

using ThemeValue = std::variant<Color, float, Edges, FontId, Size, FontSize>;

enum class ColorToken : std::uint8_t {
    surface, control, hover, pressed, disabled, accent, border, text, text_disabled,
    image, selection, caret, composition, dock_background, dock_area, dock_tab,
    dock_tab_active, dock_divider, dock_marker, dock_marker_border, count
};

enum class MetricToken : std::uint8_t {
    radius, border_width, dock_marker_border_width, dock_radius, dock_tab_radius, spacing, count
};

enum class PaddingToken : std::uint8_t { standard, count };
enum class FontSizeToken : std::uint8_t { standard, count };

using ColorStyle = std::variant<Color, ColorToken>;
using MetricStyle = std::variant<float, MetricToken>;
using PaddingStyle = std::variant<Edges, PaddingToken>;
using FontSizeStyle = std::variant<FontSize, FontSizeToken>;

template <typename T>
struct StateValues {
    std::optional<T> normal;
    std::optional<T> hovered;
    std::optional<T> pressed;
    std::optional<T> focused;
    std::optional<T> disabled;
    std::optional<T> checked;

    friend bool operator==(const StateValues&, const StateValues&) = default;
};

struct CheckboxMetrics {
    std::optional<Size> indicator_size;
    std::optional<float> indicator_gap;
    friend bool operator==(const CheckboxMetrics&, const CheckboxMetrics&) = default;
};

struct SliderMetrics {
    std::optional<float> track_thickness;
    std::optional<float> thumb_width;
    std::optional<float> thumb_inset;
    friend bool operator==(const SliderMetrics&, const SliderMetrics&) = default;
};

struct MenuMetrics {
    std::optional<Size> indicator_size;
    std::optional<float> leading_width;
    std::optional<float> trailing_width;
    std::optional<float> column_gap;
    friend bool operator==(const MenuMetrics&, const MenuMetrics&) = default;
};

// Empty fields inherit from the less-specific style. Active colors are
// selected in disabled, pressed, hovered, checked, focused, normal order;
// each state falls back through element, component and Truffle defaults.
// Token references stay live when a palette or shared token changes.
struct ComponentStyle {
    StateValues<ColorStyle> fill;
    StateValues<ColorStyle> border;
    StateValues<ColorStyle> text;
    StateValues<ColorStyle> indicator;
    StateValues<ColorStyle> indicator_border;
    StateValues<ColorStyle> selection;
    StateValues<ColorStyle> caret;
    StateValues<ColorStyle> composition;
    std::optional<MetricStyle> border_width;
    std::optional<MetricStyle> corner_radius;
    std::optional<FontSizeStyle> font_size;
    std::optional<PaddingStyle> padding;
    std::optional<Size> min_size;
    CheckboxMetrics checkbox;
    SliderMetrics slider;
    MenuMetrics menu;

    friend bool operator==(const ComponentStyle&, const ComponentStyle&) = default;
};

enum class Component : std::uint8_t {
    panel, label, image, button, checkbox, slider, scroll_area, text_edit,
    multiline_text_edit, menu_bar, menu, menu_button, menu_item, menu_separator,
    dock_background, dock_area, dock_tab, dock_divider, dock_marker, count
};

struct ComponentState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool disabled = false;
    bool checked = false;

    friend bool operator==(const ComponentState&, const ComponentState&) = default;
};

enum class StylePart : std::uint8_t {
    automatic, fill, border, text, indicator, indicator_border, selection, caret, composition
};

struct StyleStats {
    std::size_t resolved = 0; // elements whose pending properties were resolved
};

} // namespace truffle::gui

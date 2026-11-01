#pragma once

#include "truffle/gui/text.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace truffle::gui {

enum class Property : std::uint8_t {
    fill, border_color, border_width, corner_radius, padding, spacing, font, text_color,
    count
};

using ThemeValue = std::variant<Color, float, Edges, FontId>;

struct StyleStats {
    std::size_t resolved = 0; // elements whose pending properties were resolved
};

} // namespace truffle::gui

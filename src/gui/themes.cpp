#include "truffle/gui/themes.hpp"

#include <array>
#include <utility>

namespace truffle::gui {
namespace {

using Token = std::pair<std::string_view, ThemeValue>;

constexpr Color grey(float v, float a = 1.0F) { return {v, v, v, a}; }

// Interaction states step lightness; accent is the strongest neutral contrast.
const std::array<Token, 26>& tokens(BuiltinTheme theme) {
    static const std::array<Token, 26> dark{{
        {"gui.surface", grey(0.12F)},
        {"gui.control", grey(0.19F)},
        {"gui.hover", grey(0.25F)},
        {"gui.pressed", grey(0.15F)},
        {"gui.disabled", grey(0.14F)},
        {"gui.accent", grey(0.82F)},
        {"gui.border", grey(0.30F)},
        {"gui.text", grey(0.92F)},
        {"gui.text_disabled", grey(0.50F)},
        {"gui.image", grey(1.0F)},
        {"gui.selection", grey(0.45F, 0.55F)},
        {"gui.caret", grey(0.92F)},
        {"gui.composition", grey(0.75F)},
        {"gui.radius", 4.0F},
        {"gui.border_width", 1.0F},
        {"gui.padding", Edges{8, 8, 8, 8}},
        {"dock.background", grey(0.07F)},
        {"dock.area", grey(0.12F)},
        {"dock.tab", grey(0.16F)},
        {"dock.tab_active", grey(0.24F)},
        {"dock.divider", grey(0.07F)},
        {"dock.marker", grey(0.85F, 0.18F)},
        {"dock.marker_border", grey(0.85F, 0.85F)},
        {"dock.marker_border_width", 2.0F},
        {"dock.radius", 0.0F},
        {"dock.tab_radius", 0.0F},
    }};
    static const std::array<Token, 26> light{{
        {"gui.surface", grey(0.96F)},
        {"gui.control", grey(0.90F)},
        {"gui.hover", grey(0.85F)},
        {"gui.pressed", grey(0.79F)},
        {"gui.disabled", grey(0.93F)},
        {"gui.accent", grey(0.22F)},
        {"gui.border", grey(0.72F)},
        {"gui.text", grey(0.10F)},
        {"gui.text_disabled", grey(0.55F)},
        {"gui.image", grey(1.0F)},
        {"gui.selection", grey(0.55F, 0.40F)},
        {"gui.caret", grey(0.10F)},
        {"gui.composition", grey(0.35F)},
        {"gui.radius", 4.0F},
        {"gui.border_width", 1.0F},
        {"gui.padding", Edges{8, 8, 8, 8}},
        {"dock.background", grey(0.84F)},
        {"dock.area", grey(0.96F)},
        {"dock.tab", grey(0.89F)},
        {"dock.tab_active", grey(1.0F)},
        {"dock.divider", grey(0.80F)},
        {"dock.marker", grey(0.15F, 0.14F)},
        {"dock.marker_border", grey(0.15F, 0.85F)},
        {"dock.marker_border_width", 2.0F},
        {"dock.radius", 0.0F},
        {"dock.tab_radius", 0.0F},
    }};
    return theme == BuiltinTheme::truffle_light ? light : dark;
}

} // namespace

std::string_view builtin_theme_name(BuiltinTheme theme) noexcept {
    return theme == BuiltinTheme::truffle_light ? "Truffle Light" : "Truffle Dark";
}

base::Status apply_theme(Context& context, BuiltinTheme theme) {
    const auto& values = tokens(theme);
    for (const auto& [name, value] : values) {
        const auto existing = context.theme_value(name);
        if (existing.ok() && existing.value().index() != value.index()) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "GUI theme value types cannot change");
        }
    }
    for (const auto& [name, value] : values) {
        if (auto status = context.set_theme_value(std::string(name), value); !status.ok()) {
            return status;
        }
    }
    return base::Status::success();
}

base::Status install_default_theme(Context& context) {
    for (const auto& [name, value] : tokens(BuiltinTheme::truffle_dark)) {
        if (context.theme_value(name).ok()) { continue; }
        if (auto status = context.set_theme_value(std::string(name), value); !status.ok()) {
            return status;
        }
    }
    return base::Status::success();
}

} // namespace truffle::gui

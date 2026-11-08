#pragma once

#include "truffle/gui/context.hpp"

#include <cstdint>
#include <string_view>

namespace truffle::gui {

// Neutral (black/grey/white) token sets shipped with Truffle GUI. Tokens are
// ordinary theme values: applications may override any of them afterwards.
enum class BuiltinTheme : std::uint8_t { truffle_dark, truffle_light };

[[nodiscard]] std::string_view builtin_theme_name(BuiltinTheme theme) noexcept;

// Selects the palette and supplies missing defaults, without replacing existing
// metrics or component overrides. A conflicting token type is rejected before
// any values change.
[[nodiscard]] base::Status apply_theme(Context& context, BuiltinTheme theme);

// Defines only the tokens still missing, using Truffle Dark. Widgets, TextEdit
// and DockSpace call this so application-set values are never replaced.
[[nodiscard]] base::Status install_default_theme(Context& context);

} // namespace truffle::gui

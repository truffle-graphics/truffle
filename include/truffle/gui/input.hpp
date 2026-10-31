#pragma once

#include "truffle/gui/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace truffle::gui {

// Input the host forwards to the GUI. Positions are logical units relative to
// the root's top-left corner; the host converts from its platform coordinates.

enum class Modifiers : std::uint8_t {
    none = 0,
    shift = 1U << 0U,
    control = 1U << 1U,
    alt = 1U << 2U,   // Option on macOS
    super = 1U << 3U, // Command on macOS, Windows key elsewhere
};

[[nodiscard]] constexpr Modifiers operator|(Modifiers lhs, Modifiers rhs) noexcept {
    return static_cast<Modifiers>(static_cast<std::uint8_t>(lhs) |
                                  static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool has(Modifiers modifiers, Modifiers flag) noexcept {
    return (static_cast<std::uint8_t>(modifiers) & static_cast<std::uint8_t>(flag)) != 0U;
}

enum class PointerButton : std::uint8_t { none, primary, secondary, middle, back, forward };
enum class PointerKind : std::uint8_t { mouse, touch, pen };
enum class PointerAction : std::uint8_t {
    move,
    down,
    up,
    leave,  // the pointer left the GUI's area
    cancel, // the platform took the pointer away; pressed buttons are released
};

struct PointerEvent {
    PointerAction action = PointerAction::move;
    Point position;
    PointerButton button = PointerButton::none; // for down and up
    Modifiers modifiers = Modifiers::none;
    PointerKind kind = PointerKind::mouse;
    std::uint32_t pointer = 0; // distinguishes simultaneous touches or pens
};

// Scroll amounts: logical units when `precise` (trackpads), otherwise lines.
struct WheelEvent {
    Point position;
    float delta_x = 0.0F;
    float delta_y = 0.0F;
    bool precise = false;
    Modifiers modifiers = Modifiers::none;
};

// Physical keys, independent of keyboard layout. Text comes from TextEvent.
enum class Key : std::uint16_t {
    unknown,
    a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
    digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
    space, enter, escape, tab, backspace, delete_forward, insert,
    home, end, page_up, page_down, left, right, up, down,
    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    shift, control, alt, super,
};

struct KeyEvent {
    Key key = Key::unknown;
    bool down = true;
    bool repeat = false;
    Modifiers modifiers = Modifiers::none;
    std::uint32_t native = 0; // platform key code, for keys Truffle does not name
};

// Committed UTF-8 text. One key may produce no text or several characters.
struct TextEvent {
    std::string text;
};

// Text an input method is still composing. The selection is a byte range in
// `preedit`. An empty `preedit` ends composition; the result arrives as a
// TextEvent.
struct CompositionEvent {
    std::string preedit;
    std::size_t selection_begin = 0;
    std::size_t selection_end = 0;
};

// The host window gained or lost keyboard focus. Losing it releases pressed
// buttons and modifiers and ends composition.
struct FocusEvent {
    bool focused = true;
};

using InputEvent = std::variant<PointerEvent, WheelEvent, KeyEvent, TextEvent,
                                CompositionEvent, FocusEvent>;

// Input state as last reported by the host.
struct InputState {
    std::optional<Point> pointer; // empty while the pointer is outside the GUI
    std::uint8_t buttons = 0;     // bit (1 << PointerButton) per pressed button
    Modifiers modifiers = Modifiers::none;
    bool focused = true;
    CompositionEvent composition; // empty preedit when not composing

    [[nodiscard]] bool pressed(PointerButton button) const noexcept {
        return (buttons & (1U << static_cast<unsigned>(button))) != 0U;
    }
};

} // namespace truffle::gui

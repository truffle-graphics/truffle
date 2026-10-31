#pragma once

#include "truffle/base/handle.hpp"

#include <cstdint>

namespace truffle::gui {

struct ElementTag {};

// Checked reference to an element. The value packs a slot index and a
// generation, so a handle to a destroyed element never matches a reused slot.
using ElementId = base::Handle<ElementTag>;

// Work an element needs before it can be drawn again. Flags are independent so
// a change marks only what it affects.
enum class Dirty : std::uint8_t {
    none = 0,
    style = 1U << 0U,   // resolved style values must be recomputed
    measure = 1U << 1U, // desired size may have changed
    arrange = 1U << 2U, // children must be placed again
    shape = 1U << 3U,   // stored drawing shapes must be regenerated
};

[[nodiscard]] constexpr Dirty operator|(Dirty lhs, Dirty rhs) noexcept {
    return static_cast<Dirty>(static_cast<std::uint8_t>(lhs) |
                              static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr Dirty operator&(Dirty lhs, Dirty rhs) noexcept {
    return static_cast<Dirty>(static_cast<std::uint8_t>(lhs) &
                              static_cast<std::uint8_t>(rhs));
}

constexpr Dirty& operator|=(Dirty& lhs, Dirty rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool any(Dirty flags) noexcept {
    return flags != Dirty::none;
}

[[nodiscard]] constexpr bool has(Dirty flags, Dirty flag) noexcept {
    return any(flags & flag);
}

} // namespace truffle::gui

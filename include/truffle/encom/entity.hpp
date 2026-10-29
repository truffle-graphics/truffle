#pragma once

#include <cstdint>
#include <limits>

namespace truffle::encom {

struct Entity {
    static constexpr std::uint32_t invalid_index =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = invalid_index;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index;
    }

    friend constexpr bool operator==(Entity, Entity) = default;
};

} // namespace truffle::encom


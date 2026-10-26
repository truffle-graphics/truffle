#pragma once

#include <cstdint>
#include <limits>

namespace truffle::base {

template <typename Tag>
class Handle {
public:
    static constexpr std::uint64_t invalid_value =
        std::numeric_limits<std::uint64_t>::max();

    constexpr Handle() = default;
    explicit constexpr Handle(std::uint64_t value) : value_(value) {}

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != invalid_value;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    friend constexpr bool operator==(Handle, Handle) = default;

private:
    std::uint64_t value_ = invalid_value;
};

} // namespace truffle::base


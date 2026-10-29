#pragma once

#include <cstdint>

namespace truffle::base {

struct ApiVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
};

[[nodiscard]] constexpr std::uint32_t pack_api_version(ApiVersion version) noexcept {
    return (static_cast<std::uint32_t>(version.major) << 22u) |
           (static_cast<std::uint32_t>(version.minor) << 12u) |
           static_cast<std::uint32_t>(version.patch);
}

inline constexpr ApiVersion kApiVersion{0, 1, 0};
inline constexpr std::uint32_t kApiVersionPacked = pack_api_version(kApiVersion);

} // namespace truffle::base

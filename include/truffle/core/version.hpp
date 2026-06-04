#pragma once

#include <cstdint>

namespace truffle::core {

struct ApiVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
};

struct DeprecationWindow {
    std::uint16_t introducedMinor = 0;
    std::uint16_t deprecatedMinor = 0;
    std::uint16_t removedMinor = 0;
};

[[nodiscard]] constexpr std::uint32_t pack_api_version(ApiVersion version) noexcept {
    return (static_cast<std::uint32_t>(version.major) << 22u) |
           (static_cast<std::uint32_t>(version.minor) << 12u) |
           static_cast<std::uint32_t>(version.patch);
}

inline constexpr ApiVersion kApiVersion{0, 1, 0};
inline constexpr std::uint32_t kApiVersionPacked = pack_api_version(kApiVersion);

[[nodiscard]] constexpr bool is_api_compatible(
    ApiVersion requested,
    ApiVersion provided = kApiVersion) noexcept {
    // Same-major, forward-minor compatibility model.
    return requested.major == provided.major &&
           requested.minor <= provided.minor;
}

[[nodiscard]] constexpr bool is_deprecation_window_valid(
    DeprecationWindow window) noexcept {
    return window.introducedMinor <= window.deprecatedMinor &&
           window.deprecatedMinor < window.removedMinor;
}

[[nodiscard]] constexpr bool is_symbol_available(
    ApiVersion consumer,
    DeprecationWindow window,
    ApiVersion provided = kApiVersion) noexcept {
    if (!is_deprecation_window_valid(window) || consumer.major != provided.major) {
        return false;
    }
    return consumer.minor >= window.introducedMinor &&
           consumer.minor < window.removedMinor;
}

[[nodiscard]] constexpr bool is_symbol_deprecated(
    ApiVersion consumer,
    DeprecationWindow window,
    ApiVersion provided = kApiVersion) noexcept {
    if (!is_symbol_available(consumer, window, provided)) {
        return false;
    }
    return consumer.minor >= window.deprecatedMinor;
}

} // namespace truffle::core

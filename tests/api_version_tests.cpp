#include "test_support.hpp"

#include "truffle/base/version.hpp"

int main() {
    constexpr auto packed = truffle::base::pack_api_version({1, 2, 3});
    TRUFFLE_CHECK(packed == ((1u << 22u) | (2u << 12u) | 3u));

    static_assert(truffle::base::kApiVersion.major == 0);
    static_assert(truffle::base::kApiVersion.minor == 1);
    static_assert(truffle::base::kApiVersion.patch == 0);
    static_assert(truffle::base::kApiVersionPacked ==
                  truffle::base::pack_api_version(truffle::base::kApiVersion));

    TRUFFLE_CHECK(truffle::base::is_api_compatible({0, 1, 0}));
    TRUFFLE_CHECK(truffle::base::is_api_compatible({0, 0, 9}));
    TRUFFLE_CHECK(!truffle::base::is_api_compatible({1, 0, 0}));
    TRUFFLE_CHECK(!truffle::base::is_api_compatible({0, 2, 0}));

    constexpr truffle::base::DeprecationWindow stableWindow{
        .introducedMinor = 0,
        .deprecatedMinor = 2,
        .removedMinor = 4,
    };
    static_assert(truffle::base::is_deprecation_window_valid(stableWindow));
    TRUFFLE_CHECK(truffle::base::is_symbol_available({0, 1, 0}, stableWindow,
                                                      {0, 3, 0}));
    TRUFFLE_CHECK(truffle::base::is_symbol_deprecated({0, 3, 0}, stableWindow,
                                                       {0, 3, 0}));
    TRUFFLE_CHECK(!truffle::base::is_symbol_deprecated({0, 1, 0}, stableWindow,
                                                        {0, 3, 0}));
    TRUFFLE_CHECK(!truffle::base::is_symbol_available({0, 4, 0}, stableWindow,
                                                       {0, 4, 0}));

    constexpr truffle::base::DeprecationWindow invalidWindow{
        .introducedMinor = 2,
        .deprecatedMinor = 1,
        .removedMinor = 3,
    };
    TRUFFLE_CHECK(!truffle::base::is_deprecation_window_valid(invalidWindow));
    TRUFFLE_CHECK(!truffle::base::is_symbol_available({0, 2, 0}, invalidWindow,
                                                       {0, 3, 0}));

    return 0;
}

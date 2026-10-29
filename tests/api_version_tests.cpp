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

    return 0;
}

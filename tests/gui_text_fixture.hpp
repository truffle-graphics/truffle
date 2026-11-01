#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace truffle::tests {

inline std::vector<std::byte> gui_test_font() {
    constexpr std::string_view bdf = R"(STARTFONT 2.1
FONT -truffle-test-medium-r-normal--7-70-75-75-c-50-iso10646-1
SIZE 7 75 75
FONTBOUNDINGBOX 5 7 0 0
STARTPROPERTIES 2
FONT_ASCENT 7
FONT_DESCENT 0
ENDPROPERTIES
CHARS 3
STARTCHAR A
ENCODING 65
SWIDTH 500 0
DWIDTH 5 0
BBX 5 7 0 0
BITMAP
70
88
88
F8
88
88
88
ENDCHAR
STARTCHAR B
ENCODING 66
SWIDTH 500 0
DWIDTH 5 0
BBX 5 7 0 0
BITMAP
F0
88
88
F0
88
88
F0
ENDCHAR
STARTCHAR space
ENCODING 32
SWIDTH 300 0
DWIDTH 3 0
BBX 0 0 0 0
ENDCHAR
ENDFONT
)";
    const auto bytes = std::as_bytes(std::span{bdf.data(), bdf.size()});
    return {bytes.begin(), bytes.end()};
}

} // namespace truffle::tests

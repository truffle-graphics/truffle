#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace truffle::tests {

// Original, generated monochrome test artwork. No system or redistributed font.
inline std::vector<std::byte> gui_unicode_font(std::vector<std::uint32_t> codepoints) {
    std::sort(codepoints.begin(), codepoints.end());
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
    std::string bdf =
        "STARTFONT 2.1\nFONT -truffle-unicode-medium-r-normal--7-70-75-75-c-50-iso10646-1\n"
        "SIZE 7 75 75\nFONTBOUNDINGBOX 5 7 0 0\nSTARTPROPERTIES 2\n"
        "FONT_ASCENT 7\nFONT_DESCENT 0\nENDPROPERTIES\nCHARS " +
        std::to_string(codepoints.size()) + "\n";
    for (const auto cp : codepoints) {
        bdf += "STARTCHAR uni" + std::to_string(cp) + "\nENCODING " +
               std::to_string(cp) + "\nSWIDTH 500 0\nDWIDTH " +
               (cp == 0x301 ? std::string{"0"} : std::string{"5"}) +
               " 0\nBBX 5 7 0 0\nBITMAP\nF8\n88\nA8\n88\nA8\n88\nF8\nENDCHAR\n";
    }
    bdf += "ENDFONT\n";
    const auto bytes = std::as_bytes(std::span{bdf.data(), bdf.size()});
    return {bytes.begin(), bytes.end()};
}

inline std::vector<std::byte> gui_unicode_font() {
    std::vector<std::uint32_t> codepoints{
        0x20, 0x28, 0x29, 0x31, 0x32, 0x41, 0x42, 0x65, 0xe9, 0x301,
        0x5d0, 0x5d1, 0x5d2, 0x627, 0x628, 0x644, 0x645, 0x633};
    // Arabic fallback shaping can use these original presentation-form glyphs.
    for (std::uint32_t cp = 0xfe70; cp <= 0xfefc; ++cp) {
        codepoints.push_back(cp);
    }
    return gui_unicode_font(std::move(codepoints));
}

inline std::vector<std::byte> gui_ligature_font() {
    std::ifstream file{std::filesystem::path{__FILE__}.parent_path() /
                           "fixtures/unicode/TruffleUnicodeTest.ttf", std::ios::binary};
    const std::string contents{std::istreambuf_iterator<char>{file},
                               std::istreambuf_iterator<char>{}};
    const auto bytes = std::as_bytes(std::span{contents.data(), contents.size()});
    return {bytes.begin(), bytes.end()};
}

} // namespace truffle::tests

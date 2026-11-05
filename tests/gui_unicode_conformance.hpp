#pragma once

#include "../src/gui/text_unicode.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace truffle::tests {

inline void append_test_utf8(std::string& text, std::uint32_t cp) {
    if (cp < 0x80) {
        text.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        text.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        text.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        text.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        text.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

inline int gui_unicode_conformance() {
    auto context = make_null_context();
    auto engine = require(gui::TextEngine::create(context.device, {{128, 128}}));
    const auto font = require(engine.load_font(gui_test_font(), 7));
    const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path();
    std::ifstream graphemes{root / "tests/fixtures/unicode/GraphemeBreakTest-16.0.0.txt"};
    TFL_CHECK(graphemes.good());
    std::string line;
    std::size_t cases = 0;
    while (std::getline(graphemes, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream tokens{line};
        std::string token;
        std::string utf8;
        std::vector<std::size_t> expected;
        while (tokens >> token) {
            if (token == "\xc3\xb7") {
                expected.push_back(utf8.size());
            } else if (token != "\xc3\x97") {
                append_test_utf8(utf8, static_cast<std::uint32_t>(std::stoul(token, nullptr, 16)));
            }
        }
        if (expected.empty()) {
            continue;
        }
        TFL_CHECK(require(gui::text_graphemes(utf8)) == expected);
        const auto shaped = require(engine.shape(font, utf8));
        for (const auto& caret : shaped.carets) {
            TFL_CHECK(std::binary_search(expected.begin(), expected.end(), caret.byte));
        }
        for (const auto boundary : expected) {
            const auto found = std::any_of(shaped.carets.begin(), shaped.carets.end(),
                                          [&](const auto& caret) { return caret.byte == boundary; });
            if (!found) {
                std::cerr << "Missing byte " << boundary << " caret for " << line << '\n';
            }
            TFL_CHECK(found);
        }
        ++cases;
    }
    TFL_CHECK(cases == 1093);

    // Exercise our UTF-8/paragraph/script intersection glue against every
    // auto-LTR case from the pinned upstream Unicode UAX #9 character suite.
    std::ifstream bidi{root / "vendor/sheenbidi/Tools/Unicode/BidiCharacterTest.txt"};
    TFL_CHECK(bidi.good());
    cases = 0;
    while (std::getline(bidi, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields{line};
        std::array<std::string, 5> field;
        for (auto& value : field) {
            std::getline(fields, value, ';');
        }
        if (field[1] != "2") {
            continue;
        }
        std::istringstream codepoints{field[0]};
        std::string value;
        std::string utf8;
        while (codepoints >> value) {
            append_test_utf8(utf8, static_cast<std::uint32_t>(std::stoul(value, nullptr, 16)));
        }
        std::istringstream levels_stream{field[3]};
        std::vector<std::string> levels;
        while (levels_stream >> value) {
            levels.push_back(value);
        }
        std::istringstream expected_stream{field[4]};
        std::vector<std::size_t> expected;
        std::size_t index = 0;
        while (expected_stream >> index) {
            expected.push_back(index);
        }
        const auto input = require(gui::unicode::decode(utf8));
        const auto lines = require(gui::unicode::visual_lines(utf8, input));
        TFL_CHECK(lines.size() == 1);
        std::vector<std::size_t> actual;
        for (const auto& run : lines[0].runs) {
            std::vector<std::size_t> indices;
            for (std::size_t i = 0; i < input.codepoints.size(); ++i) {
                if (input.codepoints[i].begin >= run.begin && input.codepoints[i].begin < run.end &&
                    levels[i] != "x") {
                    indices.push_back(i);
                }
            }
            if (run.rtl) {
                std::reverse(indices.begin(), indices.end());
            }
            actual.insert(actual.end(), indices.begin(), indices.end());
        }
        TFL_CHECK(actual == expected);
        ++cases;
    }
    TFL_CHECK(cases == 28);
    return 0;
}

} // namespace truffle::tests

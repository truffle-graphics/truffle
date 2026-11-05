#pragma once

#include "truffle/gui/text.hpp"

namespace truffle::gui::unicode {

struct Codepoint {
    std::uint32_t value;
    std::size_t begin;
    std::size_t end;
};

struct Input {
    std::vector<Codepoint> codepoints;
    std::vector<std::size_t> graphemes;
};

struct Run {
    std::size_t begin;
    std::size_t end;
    std::uint32_t script;
    bool rtl;
};

struct Line {
    std::size_t begin;
    std::size_t end;
    std::vector<Run> runs;
};

[[nodiscard]] base::Result<Input> decode(std::string_view utf8);
[[nodiscard]] base::Result<std::vector<Line>> visual_lines(std::string_view utf8,
                                                         const Input& input);
[[nodiscard]] bool ignorable(std::uint32_t codepoint) noexcept;

} // namespace truffle::gui::unicode

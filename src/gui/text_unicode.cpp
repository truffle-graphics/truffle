#include "text_unicode.hpp"

#include <algorithm>
#include <limits>
#include <memory>

#if TFL_GUI_HAS_TEXT
#include <SheenBidi/SheenBidi.h>
#include <utf8proc.h>
#endif

namespace truffle::gui::unicode {

base::Result<Input> decode(std::string_view utf8) {
#if TFL_GUI_HAS_TEXT
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return base::Status::failure(base::StatusCode::invalid_argument, "text input is too large");
    }
    Input result;
    result.graphemes.push_back(0);
    utf8proc_int32_t previous = 0;
    utf8proc_int32_t state = 0;
    for (std::size_t offset = 0; offset < utf8.size();) {
        utf8proc_int32_t current = 0;
        const auto count = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t*>(utf8.data() + offset),
            static_cast<utf8proc_ssize_t>(utf8.size() - offset), &current);
        if (count <= 0) {
            return base::Status::failure(base::StatusCode::invalid_argument,
                                         "text input contains malformed UTF-8");
        }
        if (offset != 0 && utf8proc_grapheme_break_stateful(previous, current, &state)) {
            result.graphemes.push_back(offset);
        }
        const auto end = offset + static_cast<std::size_t>(count);
        result.codepoints.push_back({static_cast<std::uint32_t>(current), offset, end});
        offset = end;
        previous = current;
    }
    if (!utf8.empty()) {
        result.graphemes.push_back(utf8.size());
    }
    return result;
#else
    static_cast<void>(utf8);
    return base::Status::failure(base::StatusCode::unsupported, "GUI Unicode text is disabled");
#endif
}

bool ignorable(std::uint32_t codepoint) noexcept {
#if TFL_GUI_HAS_TEXT
    const auto* property = utf8proc_get_property(static_cast<utf8proc_int32_t>(codepoint));
    return property->ignorable || property->category == UTF8PROC_CATEGORY_CC ||
           property->category == UTF8PROC_CATEGORY_CF;
#else
    static_cast<void>(codepoint);
    return false;
#endif
}

base::Result<std::vector<Line>> visual_lines(std::string_view utf8, const Input& input) {
#if TFL_GUI_HAS_TEXT
    const auto allocation_error = [] {
        return base::Status::failure(base::StatusCode::out_of_memory,
                                     "Unicode bidi/script allocation failed");
    };
    std::vector<Line> lines;
    if (utf8.empty()) {
        lines.push_back({0, 0, {}});
        return lines;
    }
    const SBCodepointSequence sequence{SBStringEncodingUTF8, utf8.data(), utf8.size()};
    const auto algorithm = std::unique_ptr<_SBAlgorithm, decltype(&SBAlgorithmRelease)>{
        SBAlgorithmCreate(&sequence), SBAlgorithmRelease};
    const auto locator = std::unique_ptr<_SBScriptLocator, decltype(&SBScriptLocatorRelease)>{
        SBScriptLocatorCreate(), SBScriptLocatorRelease};
    if (!algorithm || !locator) {
        return allocation_error();
    }
    std::size_t paragraph_begin = 0;
    while (paragraph_begin < utf8.size()) {
        SBUInteger length = 0;
        SBUInteger separator = 0;
        SBAlgorithmGetParagraphBoundary(algorithm.get(), paragraph_begin,
                                         utf8.size() - paragraph_begin, &length, &separator);
        const auto paragraph = std::unique_ptr<_SBParagraph, decltype(&SBParagraphRelease)>{
            SBAlgorithmCreateParagraph(algorithm.get(), paragraph_begin, length, SBLevelDefaultLTR),
            SBParagraphRelease};
        if (!paragraph || length == 0) {
            return allocation_error();
        }
        const std::size_t content_end = paragraph_begin + length - separator;
        std::size_t line_begin = paragraph_begin;
        for (;;) {
            auto line_end = content_end;
            auto next = content_end;
            auto cp = std::lower_bound(input.codepoints.begin(), input.codepoints.end(), line_begin,
                [](const auto& value, std::size_t byte) { return value.begin < byte; });
            for (; cp != input.codepoints.end() && cp->begin < content_end; ++cp) {
                if (cp->value == 0x2028) {
                    line_end = cp->begin;
                    next = cp->end;
                    break;
                }
            }
            Line result{line_begin, line_end, {}};
            if (line_end > line_begin) {
                const auto line = std::unique_ptr<_SBLine, decltype(&SBLineRelease)>{
                    SBParagraphCreateLine(paragraph.get(), line_begin, line_end - line_begin),
                    SBLineRelease};
                if (!line) {
                    return allocation_error();
                }
                const SBCodepointSequence line_sequence{
                    SBStringEncodingUTF8, utf8.data() + line_begin, line_end - line_begin};
                SBScriptLocatorLoadCodepoints(locator.get(), &line_sequence);
                std::vector<Run> scripts;
                while (SBScriptLocatorMoveNext(locator.get())) {
                    const auto* script = SBScriptLocatorGetAgent(locator.get());
                    scripts.push_back({line_begin + script->offset,
                                       line_begin + script->offset + script->length,
                                       SBScriptGetUnicodeTag(script->script), false});
                }
                const auto* runs = SBLineGetRunsPtr(line.get());
                for (std::size_t i = 0; i < SBLineGetRunCount(line.get()); ++i) {
                    const auto& bidi = runs[i];
                    std::vector<Run> intersections;
                    auto script = std::lower_bound(scripts.begin(), scripts.end(), bidi.offset,
                        [](const auto& value, std::size_t byte) { return value.end <= byte; });
                    for (; script != scripts.end() && script->begin < bidi.offset + bidi.length;
                         ++script) {
                        const auto begin = std::max(script->begin, bidi.offset);
                        const auto end = std::min(script->end, bidi.offset + bidi.length);
                        if (begin < end) {
                            intersections.push_back({begin, end, script->script,
                                                     (bidi.level & 1U) != 0});
                        }
                    }
                    if ((bidi.level & 1U) != 0) {
                        std::reverse(intersections.begin(), intersections.end());
                    }
                    result.runs.insert(result.runs.end(), intersections.begin(), intersections.end());
                }
            }
            lines.push_back(std::move(result));
            if (line_end == content_end) {
                break;
            }
            line_begin = next;
        }
        paragraph_begin += length;
        if (paragraph_begin == utf8.size() && separator != 0) {
            lines.push_back({utf8.size(), utf8.size(), {}});
        }
    }
    return lines;
#else
    static_cast<void>(utf8);
    static_cast<void>(input);
    return base::Status::failure(base::StatusCode::unsupported, "GUI Unicode text is disabled");
#endif
}

} // namespace truffle::gui::unicode

namespace truffle::gui {

base::Result<std::vector<std::size_t>> text_graphemes(std::string_view utf8) {
    auto input = unicode::decode(utf8);
    if (!input.ok()) {
        return input.status();
    }
    return std::move(input).value().graphemes;
}

} // namespace truffle::gui

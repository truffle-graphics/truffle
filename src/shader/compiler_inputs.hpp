#pragma once

#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <cctype>

namespace truffle::shader::detail {

inline void field(std::string& output, std::string_view value) {
    output += std::to_string(value.size()) + ":";
    output += value;
}

inline rhi::Result<std::vector<rhi::ShaderDefine>> normalized_defines(
    std::span<const rhi::ShaderDefine> input) {
    std::vector<rhi::ShaderDefine> defines{input.begin(), input.end()};
    std::sort(defines.begin(), defines.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < defines.size(); ++i) {
        const auto& define = defines[i];
        const auto identifier = !define.name.empty() &&
            (std::isalpha(static_cast<unsigned char>(define.name.front())) ||
             define.name.front() == '_') &&
            std::all_of(define.name.begin(), define.name.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_';
            });
        if (!identifier || define.value.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            (i && defines[i - 1].name == define.name)) {
            return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                       "Invalid/duplicate shader define: " + define.name);
        }
    }
    return defines;
}

} // namespace truffle::shader::detail

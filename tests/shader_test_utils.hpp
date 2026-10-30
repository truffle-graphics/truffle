#pragma once

#include "truffle/rhi/status.hpp"
#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace truffle::tests {
inline bool shader_route_enabled(
    rhi::ShaderSourceLanguage language = rhi::ShaderSourceLanguage::glsl,
    rhi::ShaderTarget target = rhi::ShaderTarget::spirv) {
    const auto routes = shader::Compiler::routes();
    return std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
        return route.language == language && route.target == target;
    });
}

inline void require(base::Status status) {
    if (!status.ok()) throw std::runtime_error(status.message);
}

template <typename T>
T require(base::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().message);
    return std::move(result).value();
}

inline void shader_check(bool condition, const char* expression, int line) {
    if (!condition)
        throw std::runtime_error(std::string{expression} + " at line " + std::to_string(line));
}
} // namespace truffle::tests

#define TRUFFLE_SHADER_CHECK(condition) \
    ::truffle::tests::shader_check((condition), #condition, __LINE__)

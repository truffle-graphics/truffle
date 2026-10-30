#pragma once

#include "truffle/rhi/status.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace truffle::tests {
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

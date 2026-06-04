#pragma once

#include <optional>
#include <string>
#include <utility>

namespace truffle::core {

enum class StatusCode {
    ok,
    invalid_argument,
    unsupported,
    unavailable,
    invalid_state,
    timeout,
    backend_error,
};

struct Status {
    StatusCode code = StatusCode::ok;
    std::string message;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return code == StatusCode::ok;
    }

    [[nodiscard]] static Status success() {
        return {};
    }

    [[nodiscard]] static Status failure(StatusCode code, std::string message) {
        return Status{code, std::move(message)};
    }
};

template <typename T>
class Result {
public:
    Result(T value) : status_(Status::success()), value_(std::move(value)) {}
    Result(Status status) : status_(std::move(status)) {}

    [[nodiscard]] bool ok() const noexcept {
        return status_.ok() && value_.has_value();
    }

    [[nodiscard]] const Status& status() const noexcept {
        return status_;
    }

    [[nodiscard]] T& value() & {
        return *value_;
    }

    [[nodiscard]] const T& value() const& {
        return *value_;
    }

    [[nodiscard]] T&& value() && {
        return std::move(*value_);
    }

private:
    Status status_;
    std::optional<T> value_;
};

} // namespace truffle::core

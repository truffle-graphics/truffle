#pragma once

#include <cstdint>
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
    suboptimal,
    out_of_date,
    surface_lost,
    device_lost,
    out_of_memory,
    backend_validation_failed,
    backend_error,
};

struct StatusDetail {
    std::string domain;
    std::int64_t nativeCode = 0;
    std::string objectLabel;
    std::string message;
};

struct Status {
    StatusCode code = StatusCode::ok;
    std::string message;
    std::optional<StatusDetail> detail;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return code == StatusCode::ok;
    }

    [[nodiscard]] static Status success() {
        return {};
    }

    [[nodiscard]] static Status failure(StatusCode code, std::string message) {
        return Status{code, std::move(message), std::nullopt};
    }

    [[nodiscard]] static Status failure(StatusCode code, std::string message,
                                        StatusDetail detail) {
        return Status{code, std::move(message), std::move(detail)};
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

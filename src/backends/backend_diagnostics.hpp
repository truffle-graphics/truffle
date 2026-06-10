#pragma once

#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace truffle::rhi {

class BackendDiagnosticsState {
public:
    explicit BackendDiagnosticsState(BackendKind backend) : backend_(backend) {}

    [[nodiscard]] BackendStats stats() const noexcept { return stats_; }
    [[nodiscard]] BackendStats& mutable_stats() noexcept { return stats_; }

    [[nodiscard]] std::vector<BackendEvent> recent_events() const {
        return events_;
    }

    void clear() noexcept {
        stats_ = {};
        events_.clear();
        nextSequence_ = 1;
    }

    void record(BackendEventKind kind,
                std::string label = {},
                std::string message = {},
                core::StatusCode status = core::StatusCode::ok) {
        if (events_.size() == kMaxEvents) {
            events_.erase(events_.begin());
        }
        events_.push_back(BackendEvent{
            .sequence = nextSequence_++,
            .backend = backend_,
            .kind = kind,
            .status = status,
            .label = std::move(label),
            .message = std::move(message),
        });
    }

private:
    static constexpr std::size_t kMaxEvents = 64;

    BackendKind backend_;
    BackendStats stats_;
    std::vector<BackendEvent> events_;
    std::uint64_t nextSequence_ = 1;
};

using BackendDiagnosticsPtr = std::shared_ptr<BackendDiagnosticsState>;

[[nodiscard]] inline BackendDiagnosticsPtr make_backend_diagnostics(
    BackendKind backend) {
    return std::make_shared<BackendDiagnosticsState>(backend);
}

inline void record_backend_event(const BackendDiagnosticsPtr& diagnostics,
                                 BackendEventKind kind,
                                 std::string label = {},
                                 std::string message = {},
                                 core::StatusCode status = core::StatusCode::ok) {
    if (diagnostics) {
        diagnostics->record(kind, std::move(label), std::move(message), status);
    }
}

} // namespace truffle::rhi

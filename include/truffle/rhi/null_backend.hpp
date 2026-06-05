#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <memory>

namespace truffle::rhi {

using NullBackendStats = BackendStats;

class INullBackend : public IBackend {
public:
    [[nodiscard]] virtual NullBackendStats stats() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<INullBackend> create_null_backend();

} // namespace truffle::rhi

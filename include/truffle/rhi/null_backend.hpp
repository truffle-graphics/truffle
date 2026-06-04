#pragma once

#include "truffle/rhi/rhi.hpp"

#include <cstdint>
#include <memory>

namespace truffle::rhi {

struct NullBackendStats {
    std::uint64_t buffersCreated = 0;
    std::uint64_t texturesCreated = 0;
    std::uint64_t surfacesCreated = 0;
    std::uint64_t swapchainsCreated = 0;
    std::uint64_t commandBuffersCreated = 0;
    std::uint64_t drawsRecorded = 0;
    std::uint64_t submissions = 0;
    std::uint64_t debugLabelsPushed = 0;
    std::uint64_t debugMarkersInserted = 0;
};

class INullBackend : public IBackend {
public:
    [[nodiscard]] virtual NullBackendStats stats() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<INullBackend> create_null_backend();

} // namespace truffle::rhi

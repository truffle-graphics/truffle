#pragma once

#include "truffle/rhi/types.hpp"

#include <span>
#include <string_view>

namespace truffle::rhi {

struct BackendEvidence {
    bool compiles = false;
    bool nativeSmoke = false;
    bool conformance = false;
    bool validation = false;
    bool presentation = false;
};

struct BackendPlatformSupport {
    BackendKind backend = BackendKind::null_validation;
    PlatformKind platform = PlatformKind::all;
    BackendMaturity maturity = BackendMaturity::source_only;
    bool gpuBackend = true;
    BackendEvidence evidence;
    std::string_view notes;
};

[[nodiscard]] std::string_view backend_name(BackendKind backend) noexcept;
[[nodiscard]] std::string_view platform_name(PlatformKind platform) noexcept;
[[nodiscard]] std::string_view maturity_name(BackendMaturity maturity) noexcept;
[[nodiscard]] PlatformKind host_platform() noexcept;

// Repository evidence, not a runtime capability promise. A runtime adapter is
// exposed separately and only after its native initialization succeeds.
[[nodiscard]] std::span<const BackendPlatformSupport>
backend_platform_support() noexcept;

} // namespace truffle::rhi

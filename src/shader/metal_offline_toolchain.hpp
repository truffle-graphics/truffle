#pragma once

#include "truffle/shader/compiler.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace truffle::shader::detail {

struct OfflineMetalResult {
    std::vector<std::byte> bytes;
    std::string diagnostics;
    std::string compilerVersion;
    std::string linkerVersion;
    std::string sdkVersion;
};

struct OfflineMetalIdentity {
    std::string compilerVersion;
    std::string linkerVersion;
    std::string sdkVersion;
};

[[nodiscard]] bool offline_metallib_available() noexcept;
[[nodiscard]] rhi::Result<OfflineMetalIdentity>
offline_metallib_identity();
[[nodiscard]] rhi::Result<OfflineMetalResult> compile_metallib(
    std::string_view source, bool debugInfo);

} // namespace truffle::shader::detail

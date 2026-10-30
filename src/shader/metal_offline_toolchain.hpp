#pragma once

#include "truffle/shader/compiler.hpp"

#include <filesystem>
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

struct OfflineMetalToolchain {
    std::string metalPath;
    std::string metallibPath;
    std::filesystem::path compilerRoot;
    std::string compilerVersion;
    std::string linkerVersion;
    std::string sdkVersion;
};

[[nodiscard]] bool offline_metallib_available() noexcept;
[[nodiscard]] rhi::Result<OfflineMetalToolchain>
offline_metallib_toolchain();
[[nodiscard]] rhi::Status validate_metallib_dependencies(
    const OfflineMetalToolchain& toolchain, std::string_view source);
[[nodiscard]] rhi::Result<OfflineMetalResult> compile_metallib(
    const OfflineMetalToolchain& toolchain, std::string_view source,
    bool debugInfo);

} // namespace truffle::shader::detail

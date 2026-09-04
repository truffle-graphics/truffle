#pragma once

#include "truffle/rhi/status.hpp"
#include "truffle/rhi/types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace truffle::shaderc {

struct GlslangOutput {
    std::vector<std::byte> spirv;
    std::string compilerVersion;
    std::string compilerRevision;
    std::string diagnostics;
};

[[nodiscard]] rhi::Result<GlslangOutput> compile_glsl_to_spirv(
    std::string_view source, rhi::ShaderStage stage, std::string_view entryPoint,
    bool embeddedProfile);

} // namespace truffle::shaderc

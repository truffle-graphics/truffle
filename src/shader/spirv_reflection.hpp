#pragma once

#include "truffle/shader/compiler.hpp"

namespace truffle::shader::detail {

// Reflect the emitted module, not source spelling or glslang's live-only database.
[[nodiscard]] rhi::Status reflect_spirv(
    std::span<const unsigned int> words, rhi::ShaderStage stage,
    rhi::ShaderReflection& reflection);

} // namespace truffle::shader::detail

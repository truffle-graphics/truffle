#pragma once

#include "dxc_support.hpp"
#include "truffle/rhi/shader_package.hpp"

namespace truffle::shader::detail {

[[nodiscard]] rhi::Status reflect_dxil(IDxcUtils& utils, IDxcBlob& code,
                                      rhi::ShaderStage stage, rhi::ShaderReflection& reflection);
[[nodiscard]] rhi::Status validate_dxil(IDxcBlob& code);

} // namespace truffle::shader::detail

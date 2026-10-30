#pragma once

#include "truffle/shader/compiler.hpp"

namespace truffle::shader::detail {

rhi::Status validate_spirv(std::span<const unsigned int> words, VulkanTarget target);
rhi::Status translate_spirv(std::span<const unsigned int> words,
                            const CompileRequest& request,
                            rhi::ShaderVariantDesc& variant,
                            std::vector<rhi::ShaderBindingRemap>& remaps);

} // namespace truffle::shader::detail

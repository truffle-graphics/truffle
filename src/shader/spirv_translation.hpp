#pragma once

#include "truffle/shader/compiler.hpp"

namespace truffle::shader::detail {

rhi::Status validate_spirv(std::span<const unsigned int> words, VulkanTarget target);
rhi::Status validate_generated_glsl(const CompileRequest& request, std::span<const std::byte> code);
std::string translation_provenance();
rhi::Status translate_spirv(std::span<const unsigned int> words,
                            const CompileRequest& request,
                            rhi::ShaderVariantDesc& variant,
                            std::vector<rhi::ShaderBindingRemap>& remaps);

} // namespace truffle::shader::detail

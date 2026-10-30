#include "spirv_translation.hpp"

#include <spirv-tools/libspirv.hpp>

namespace truffle::shader::detail {

rhi::Status validate_spirv(std::span<const unsigned int> words, VulkanTarget target) {
    spvtools::SpirvTools validator{target.environment == VulkanVersion::v1_0
                                     ? SPV_ENV_VULKAN_1_0 : SPV_ENV_VULKAN_1_1};
    std::string diagnostics;
    validator.SetMessageConsumer([&](spv_message_level_t, const char*,
                                     const spv_position_t& position, const char* message) {
        if (diagnostics.size() < 16384)
            diagnostics += std::to_string(position.index) + ": " + message + "\n";
    });
    if (!validator.Validate(words.data(), words.size()))
        return rhi::Status::failure(rhi::StatusCode::invalid_argument,
                                   "SPIRV-Tools validation failed: " + diagnostics.substr(0, 16384));
    return rhi::Status::success();
}

} // namespace truffle::shader::detail

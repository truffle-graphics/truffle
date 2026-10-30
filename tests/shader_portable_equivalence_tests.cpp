#include "shader_portable_fixtures.hpp"
#include "shader_test_utils.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    using namespace truffle;
    try {
        if (argc != 9) return 1;
        const auto target = tests::require(rhi::shader_target_from_name(argv[2]));
        const auto language = tests::require(rhi::shader_source_language_from_name(argv[4]));
        const auto stage = std::string_view{argv[3]} == "vertex" ? rhi::ShaderStage::vertex :
                           std::string_view{argv[3]} == "fragment" ? rhi::ShaderStage::fragment : rhi::ShaderStage::compute;
        auto request = tests::portable_request(stage, target, language);
        if (std::string_view{argv[8]} == "write") {
            std::ofstream file{argv[1], std::ios::binary};
            file << request.source.text;
            return file ? 0 : 2;
        }
        request.source = tests::require(shader::read_source(argv[1]));
        request.vulkanTarget.environment = std::string_view{argv[5]} == "1.0"
            ? shader::VulkanVersion::v1_0 : shader::VulkanVersion::v1_1;
        request.vulkanTarget.spirv = std::string_view{argv[6]} == "1.0"
            ? shader::SpirvVersion::v1_0 : shader::SpirvVersion::v1_3;
        const auto compilation = tests::require(shader::Compiler{}.compile(request));
        std::ifstream file{argv[7], std::ios::binary};
        const std::string bytes{std::istreambuf_iterator<char>{file}, {}};
        const auto actual = compilation.package.bytes();
        TRUFFLE_SHADER_CHECK(bytes.size() == actual.size() &&
            std::equal(actual.begin(), actual.end(), bytes.begin(), [](std::byte a, char b) {
                return std::to_integer<unsigned char>(a) == static_cast<unsigned char>(b);
            }));
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
}

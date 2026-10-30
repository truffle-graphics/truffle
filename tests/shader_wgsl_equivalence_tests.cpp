#include "shader_test_utils.hpp"
#include "shader_wgsl_fixtures.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    using namespace truffle;
    try {
        if (argc != 4 && argc != 5) return 1;
        if (std::string_view{argv[1]} == "--extract") {
            const auto source = tests::require(shader::read_source(argv[2]));
            const auto bytes = std::as_bytes(std::span{source.text});
            const auto package = tests::require(rhi::ShaderPackage::load(bytes));
            const auto& variant = package.desc().variants.front();
            TRUFFLE_SHADER_CHECK(variant.format == rhi::ShaderByteFormat::spirv);
            std::ofstream output{argv[3], std::ios::binary};
            output.write(reinterpret_cast<const char*>(variant.code.data()), static_cast<std::streamsize>(variant.code.size()));
            return output.good() ? 0 : 1;
        }
        auto request = tests::wgsl_request(rhi::ShaderStage::compute);
        request.source = tests::require(shader::read_source(argv[1]));
        request.target = tests::require(rhi::shader_target_from_name(argv[2]));
        if (argc == 5) request.language = tests::require(rhi::shader_source_language_from_name(argv[4]));
        const auto compilation = tests::require(shader::Compiler{}.compile(request));
        std::ifstream stream{argv[3], std::ios::binary};
        const std::string bytes{std::istreambuf_iterator<char>{stream}, {}};
        const auto actual = compilation.package.bytes();
        TRUFFLE_SHADER_CHECK(bytes.size() == actual.size());
        TRUFFLE_SHADER_CHECK(std::equal(actual.begin(), actual.end(), bytes.begin(),
            [](std::byte a, char b) { return std::to_integer<unsigned char>(a) == static_cast<unsigned char>(b); }));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

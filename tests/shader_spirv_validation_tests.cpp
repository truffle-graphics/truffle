#include "spirv_translation.hpp"
#include "shader_test_utils.hpp"

#include <array>

int main() {
    using namespace truffle;
    constexpr std::array<unsigned int, 5> incomplete{0x07230203, 0x00010300, 0, 1, 0};
    const auto invalid = shader::detail::validate_spirv(incomplete, {});
    TRUFFLE_SHADER_CHECK(!invalid.ok() && invalid.message.find("SPIRV-Tools validation failed") != std::string::npos);
    if (shader::Compiler::available()) {
        shader::CompileRequest request;
        request.source = {"valid.vert", "#version 450\nvoid main(){gl_Position=vec4(0);}\n"};
        const auto compilation = tests::require(shader::Compiler{}.compile(request));
        TRUFFLE_SHADER_CHECK(compilation.compiler.revision.find(
            "b707790a898e44038547df54580022fc1cf89c3d") != std::string::npos);
    }
}

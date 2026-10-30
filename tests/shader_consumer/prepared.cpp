#include <truffle/renderer/shaders.hpp>
#include <truffle/renderer/source_program.hpp>
#include <truffle/rhi/shader_package.hpp>

int main() {
    // Renderer's public source-program header resolves Shader transitively.
    if (truffle::shader::Compiler::available() != static_cast<bool>(EXPECT_COMPILER)) {
        return 2;
    }
    return truffle::rhi::ShaderPackage::load({}).ok() ? 1 : 0;
}

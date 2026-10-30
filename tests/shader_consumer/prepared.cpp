#include <truffle/rhi/shader_package.hpp>
#include <truffle/renderer/shaders.hpp>

int main() {
    return truffle::rhi::ShaderPackage::load({}).ok() ? 1 : 0;
}

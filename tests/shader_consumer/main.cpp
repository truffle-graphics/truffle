#include <truffle/shader/compiler.hpp>

#include <iostream>

int main() {
    using namespace truffle;
    shader::Compiler compiler;
    if (compiler.available() != static_cast<bool>(EXPECT_COMPILER)) return 1;
    shader::CompileRequest request;
    request.source = {"installed.vert", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    auto result = compiler.compile(request);
    if (!result.ok() && EXPECT_COMPILER) {
        std::cerr << result.status().message << '\n';
        return 2;
    }
    return result.ok() == static_cast<bool>(EXPECT_COMPILER) ? 0 : 3;
}

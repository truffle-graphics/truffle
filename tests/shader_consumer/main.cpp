#include <truffle/shader/compiler.hpp>

#include <iostream>

int main() {
    using namespace truffle;
    shader::Compiler compiler;
    if (compiler.available() != static_cast<bool>(EXPECT_COMPILER)) return 1;
    if (compiler.routes().empty() == static_cast<bool>(EXPECT_COMPILER)) return 4;
    shader::CompileRequest request;
    request.source = {"installed.vert", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    auto result = compiler.compile(request);
    if (result.ok()) {
        for (const auto configuration : compiler.routes().front().vulkanTargets) {
            request.vulkanTarget = configuration;
            auto configured = compiler.compile(request);
            if (!configured.ok() || configured.value().vulkanTarget != configuration ||
                !compiler.compile(request).value().cacheHit) return 5;
        }
        request.vulkanTarget = {};
        for (const auto& route : compiler.routes()) {
            request.target = route.target;
            request.language = route.language;
            request.allowExperimentalMetalGeneration = true;
            request.source.text = route.language == rhi::ShaderSourceLanguage::glsl_es
                ? "#version 310 es\nvoid main(){gl_Position=vec4(0);}\n"
                : "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
            const auto native = compiler.compile(request);
            if (!native.ok()) {
                std::cerr << native.status().message << '\n';
                return 6;
            }
        }
    }
    if (!result.ok() && EXPECT_COMPILER) {
        std::cerr << result.status().message << '\n';
        return 2;
    }
    return result.ok() == static_cast<bool>(EXPECT_COMPILER) ? 0 : 3;
}

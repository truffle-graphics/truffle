#include <truffle/shader/compiler.hpp>

#include <iostream>

int main() {
    using namespace truffle;
    shader::Compiler compiler;
    if (compiler.available() != static_cast<bool>(EXPECT_COMPILER)) return 1;
    if (compiler.routes().empty() == static_cast<bool>(EXPECT_COMPILER)) return 4;
    shader::CompileRequest request;
    request.source = {"installed.vert", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    if (!EXPECT_COMPILER) return compiler.compile(request).ok() ? 3 : 0;
    for (const auto& route : compiler.routes()) {
        request.target = route.target;
        request.language = route.language;
        request.allowExperimentalMetalGeneration = true;
        request.source.text = route.language == rhi::ShaderSourceLanguage::slang
            ? "float4 main(uint id : SV_VulkanVertexID) : SV_Position { return float4(0,0,0,1); }\n"
            : route.language == rhi::ShaderSourceLanguage::glsl_es
                ? "#version 310 es\nvoid main(){gl_Position=vec4(0);}\n"
                : "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
        for (const auto configuration : route.vulkanTargets) {
            request.vulkanTarget = configuration;
            const auto result = compiler.compile(request);
            if (!result.ok()) {
                std::cerr << result.status().message << '\n';
                return 2;
            }
            if (result.value().vulkanTarget != configuration ||
                !compiler.compile(request).value().cacheHit) return 5;
        }
    }
}

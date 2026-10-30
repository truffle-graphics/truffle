#include <truffle/shader/compiler.hpp>

#include <iostream>

int main() {
    using namespace truffle;
    shader::Compiler compiler;
    if (compiler.available() != static_cast<bool>(EXPECT_COMPILER)) return 1;
    if (compiler.routes().empty() == static_cast<bool>(EXPECT_COMPILER)) return 4;
    if (!EXPECT_COMPILER) {
        shader::CompileRequest request;
        request.source = {"installed.vert", "#version 450\nvoid main(){gl_Position=vec4(0);}\n"};
        return compiler.compile(request).status().code == base::StatusCode::unsupported ? 0 : 3;
    }
    for (const auto& route : compiler.routes()) {
        shader::CompileRequest request;
        request.source.identity = "installed.vert";
        request.target = route.target;
        request.language = route.language;
        request.allowExperimentalMetalGeneration = true;
        request.allowExperimentalWgslGeneration = true;
        request.source.text = route.language == rhi::ShaderSourceLanguage::hlsl
            ? "float4 main():SV_Position{return float4(0,0,0,1);}\n"
            : route.language == rhi::ShaderSourceLanguage::glsl_es
                ? "#version 310 es\nvoid main(){gl_Position=vec4(0);}\n"
                : "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
        if (route.language == rhi::ShaderSourceLanguage::wgsl ||
            route.language == rhi::ShaderSourceLanguage::spirv) {
            request.source.text = "@vertex fn main() -> @builtin(position) vec4f { return vec4f(0); }";
            if (route.language == rhi::ShaderSourceLanguage::spirv) {
                auto input = request;
                input.language = rhi::ShaderSourceLanguage::wgsl;
                input.target = rhi::ShaderTarget::spirv;
                auto intermediate = compiler.compile(input);
                if (!intermediate.ok()) return 7;
                const auto& code = intermediate.value().package.desc().variants.front().code;
                request.source.text.assign(reinterpret_cast<const char*>(code.data()), code.size());
            }
        }
        const auto check = [&]() {
            const auto result = compiler.compile(request);
            if (!result.ok()) {
                std::cerr << result.status().message << '\n';
                return false;
            }
            const auto cached = compiler.compile(request);
            return result.value().vulkanTarget == request.vulkanTarget &&
                   cached.ok() && cached.value().cacheHit;
        };
        if (route.vulkanTargets.empty()) {
            if (!check()) return 5;
        } else {
            for (const auto configuration : route.vulkanTargets) {
                request.vulkanTarget = configuration;
                if (!check()) return 6;
            }
        }
    }
    return 0;
}

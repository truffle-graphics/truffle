#include <truffle/shader/compiler.hpp>

#include <iostream>

int main() {
    using namespace truffle;
    shader::Compiler compiler;
    if (compiler.available() != static_cast<bool>(EXPECT_COMPILER)) return 1;
    if (compiler.routes().empty() == static_cast<bool>(EXPECT_COMPILER)) return 4;
    shader::CompileRequest request;
    request.source = {"installed.vert", "#version 450\nvoid main(){ gl_Position=vec4(0); }\n"};
    const auto set_source = [&](rhi::ShaderSourceLanguage language) {
        request.language = language;
        if (language == rhi::ShaderSourceLanguage::wgsl || language == rhi::ShaderSourceLanguage::spirv) {
            request.source.text = "@vertex fn main() -> @builtin(position) vec4f { return vec4f(0); }";
            if (language == rhi::ShaderSourceLanguage::spirv) {
                auto input = request;
                input.language = rhi::ShaderSourceLanguage::wgsl;
                input.target = rhi::ShaderTarget::spirv;
                auto intermediate = compiler.compile(input);
                if (!intermediate.ok()) return false;
                const auto& code = intermediate.value().package.desc().variants.front().code;
                request.source.text.assign(reinterpret_cast<const char*>(code.data()), code.size());
            }
        } else {
            request.source.text = language == rhi::ShaderSourceLanguage::glsl_es
                ? "#version 310 es\nvoid main(){gl_Position=vec4(0);}\n"
                : "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
        }
        return true;
    };
    if (!compiler.routes().empty()) {
        request.target = compiler.routes().front().target;
        if (!set_source(compiler.routes().front().language)) return 7;
    }
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
            request.allowExperimentalWgslGeneration = true;
            if (!set_source(route.language)) return 7;
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

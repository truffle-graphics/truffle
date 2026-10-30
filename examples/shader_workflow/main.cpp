#include <truffle/shader/compiler.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

int failure(std::string_view message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    using namespace truffle;
    if (argc != 2) {
        std::cerr << "Usage: truffle_shader_workflow FRAGMENT_SOURCE\n";
        return 2;
    }

    const auto routes = shader::Compiler::routes();
    if (std::none_of(routes.begin(), routes.end(), [](const auto& route) {
            return route.language == rhi::ShaderSourceLanguage::glsl &&
                   route.target == rhi::ShaderTarget::spirv;
        })) {
        return failure("GLSL-to-SPIR-V compilation is unavailable; enable "
                       "TRUFFLE_SHADER_ENABLE_GLSLANG for this example");
    }

    auto source = shader::read_source(argv[1]);
    if (!source.ok()) return failure(source.status().message);
    shader::CompileRequest request;
    request.name = "shader-workflow";
    request.source = std::move(source).value();
    request.stage = rhi::ShaderStage::fragment;
    request.defines = {{"USE_TINT", "1"}};

    shader::Compiler compiler;
    shader::RebuildState rebuild;
    auto first = rebuild.rebuild(compiler, request);
    if (!first.ok()) return failure(first.status().message);
    const auto firstKey = first.value().cacheKey;
    std::cout << "Compiled requested configuration: "
              << first.value().dependencies.size() << " dependencies, "
              << first.value().package.bytes().size() << " TSP bytes\n";

    auto repeated = rebuild.rebuild(compiler, request);
    if (!repeated.ok()) return failure(repeated.status().message);
    if (!repeated.value().cacheHit || repeated.value().cacheKey != firstKey) {
        return failure("An unchanged request did not reuse its cached result");
    }

    request.defines.front().value = "0";
    auto configured = rebuild.rebuild(compiler, request);
    if (!configured.ok()) return failure(configured.status().message);
    if (configured.value().cacheHit || configured.value().cacheKey == firstKey) {
        return failure("A changed configuration reused stale output");
    }
    const auto configuredKey = configured.value().cacheKey;
    const auto root = request.source;

    request.source.text += "\n#error deliberate failed edit\n";
    const auto failed = rebuild.rebuild(compiler, request);
    if (failed.ok() || !rebuild.last_good() ||
        rebuild.last_good()->cacheKey != configuredKey) {
        return failure("A failed edit replaced the last-good compilation");
    }
    std::cout << "Expected compile rejection: " << failed.status().message << '\n';

    request.source.text =
        "#version 450\n"
        "layout(set=0,binding=0) uniform sampler2D image;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){color=texture(image,vec2(0));}\n";
    const auto unsupported = rebuild.rebuild(compiler, request);
    if (unsupported.ok() ||
        unsupported.status().code != rhi::StatusCode::unsupported ||
        !rebuild.last_good() ||
        rebuild.last_good()->cacheKey != configuredKey) {
        return failure("Unsupported reflection replaced the last-good compilation");
    }
    std::cout << "Expected reflection rejection: "
              << unsupported.status().message << '\n';

    request.source = root;
    auto recovered = rebuild.rebuild(compiler, request);
    if (!recovered.ok()) return failure(recovered.status().message);
    if (!recovered.value().cacheHit ||
        recovered.value().cacheKey != configuredKey) {
        return failure("Restoring a valid request did not recover its cached output");
    }
    std::cout << "Recovered the last-good TSP without a renderer or GPU\n";
    return 0;
}

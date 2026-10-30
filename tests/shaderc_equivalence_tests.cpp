#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    using namespace truffle;
    if (argc != 4) return 1;
    auto source = shader::read_source(argv[1]);
    if (!source.ok()) return 2;
    shader::CompileRequest request;
    request.name = "configured";
    request.source = std::move(source).value();
    request.stage = rhi::ShaderStage::compute;
    request.includeRoots = {argv[2]};
    request.defines = {{"FLAG", "1"}, {"QUALITY", "2"}};
    shader::Compiler compiler;
    auto compilation = compiler.compile(request);
    if (!compilation.ok()) {
        std::cerr << compilation.status().message << '\n';
        return 3;
    }
    std::ifstream file{argv[3], std::ios::binary};
    std::string bytes{std::istreambuf_iterator<char>{file}, {}};
    const auto actual = compilation.value().package.bytes();
    if (bytes.size() != actual.size() ||
        !std::equal(actual.begin(), actual.end(), bytes.begin(),
                    [](std::byte a, char b) { return std::to_integer<unsigned char>(a) == static_cast<unsigned char>(b); })) {
        std::cerr << "CLI and library packages differ\n";
        return 4;
    }
    const auto& reflection = compilation.value().package.desc().variants.front().reflection;
    if (compilation.value().dependencies.size() != 3 || reflection.bindings.size() != 1 ||
        reflection.requiredWorkgroupSize != rhi::Extent3D{2, 1, 1}) return 5;
    if (reflection.bindings[0].arrayCount != 2 || reflection.bindings[0].minimumSize != 4 ||
        reflection.pushConstants != std::vector<rhi::PushConstantRange>{{rhi::ShaderStage::compute, 16, 4}} ||
        reflection.specializationConstants != std::vector<rhi::ShaderSpecializationConstant>{
            {7, "increment", rhi::ShaderValueType::uint32, 9}}) return 6;
    return 0;
}

#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

template <typename T>
T require(truffle::base::Result<T> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

int run(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        throw std::runtime_error("Usage: truffle_gui_shader_package SOURCES OUTPUT [--check]");
    }
    const bool check = argc == 4;
    if (check && std::string_view(argv[3]) != "--check") {
        throw std::runtime_error("Unknown GUI package generator option");
    }
    using namespace truffle;
    shader::Compiler compiler;
    rhi::ShaderPackageDesc desc;
    desc.name = "Truffle GUI";
    for (const auto& [file, stage] :
         std::array{std::pair{"gui.vert", rhi::ShaderStage::vertex},
                    std::pair{"gui.frag", rhi::ShaderStage::fragment}}) {
        shader::CompileRequest request;
        request.name = desc.name;
        request.stage = stage;
        request.source = require(shader::read_source(
            (std::filesystem::path(argv[1]) / file).string()));
        // Stable authoring identity keeps the shipped package machine-independent.
        request.source.identity = std::string("truffle/gui/shaders/") + file;
        auto compilation = require(compiler.compile(request));
        const auto& compiled = compilation.package.desc();
        if (desc.compilers.empty()) {
            desc.compilers = compiled.compilers;
        } else if (desc.compilers != compiled.compilers) {
            throw std::runtime_error("GUI stages used different compiler identities");
        }
        desc.sources.insert(desc.sources.end(), compiled.sources.begin(), compiled.sources.end());
        desc.variants.insert(desc.variants.end(), compiled.variants.begin(), compiled.variants.end());
    }
    const auto package = require(rhi::ShaderPackage::create(std::move(desc)));
    const auto bytes = package.bytes();
    if (check) {
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot read prepared GUI Vulkan package");
        }
        const std::string existing{std::istreambuf_iterator<char>(input), {}};
        const auto existing_bytes = std::as_bytes(std::span{existing});
        if (!std::ranges::equal(bytes, existing_bytes)) {
            throw std::runtime_error("GUI Vulkan package is stale; regenerate it");
        }
        std::cout << "GUI Vulkan package regeneration is identical\n";
    } else {
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("Cannot write prepared GUI Vulkan package");
        }
        std::cout << "Generated GUI Vulkan package: " << bytes.size() << " bytes\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

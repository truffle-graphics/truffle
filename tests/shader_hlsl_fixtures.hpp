#pragma once

#include "truffle/shader/compiler.hpp"

#include <stdexcept>

namespace truffle::tests {

inline shader::CompileRequest hlsl_request(rhi::ShaderStage stage, rhi::ShaderTarget target) {
    const std::string fixture = stage == rhi::ShaderStage::vertex ? "vertex" :
                                stage == rhi::ShaderStage::fragment ? "fragment" : "compute";
    auto source = shader::read_source(std::string{TRUFFLE_HLSL_FIXTURE_DIR} + "/" + fixture + ".hlsl");
    if (!source.ok()) throw std::runtime_error(source.status().message);
    shader::CompileRequest request;
    request.name = "hlsl-" + fixture;
    request.source = std::move(source).value();
    request.language = rhi::ShaderSourceLanguage::hlsl;
    request.target = target;
    request.stage = stage;
    request.entryPoint = fixture + "Main";
    request.includeRoots = {TRUFFLE_HLSL_FIXTURE_DIR};
    request.defines = {{"QUALITY", "7"}, {"SCALE", "1.0"}};
    return request;
}

} // namespace truffle::tests

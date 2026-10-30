#pragma once
#include "truffle/shader/compiler.hpp"

namespace truffle::tests {
inline shader::CompileRequest wgsl_request(rhi::ShaderStage stage,
                                           rhi::ShaderTarget target = rhi::ShaderTarget::spirv) {
    shader::CompileRequest request;
    request.name = "wgsl";
    request.source.identity = "fixture.wgsl";
    request.language = rhi::ShaderSourceLanguage::wgsl;
    request.stage = stage;
    request.target = target;
    request.allowExperimentalMetalGeneration = true;
    request.allowExperimentalWgslGeneration = true;
    switch (stage) {
    case rhi::ShaderStage::vertex:
        request.source.text = R"(
struct Output { @builtin(position) position: vec4f, @location(0) uv: vec2f }
@vertex fn main(@location(0) position: vec2f) -> Output {
    return Output(vec4f(position, 0.0, 1.0), position * 0.5 + 0.5);
})";
        break;
    case rhi::ShaderStage::fragment:
        request.source.text = R"(
@group(1) @binding(5) var image: texture_2d<f32>;
@group(1) @binding(6) var state: sampler;
struct Tint { value: vec4f }
@group(1) @binding(7) var<uniform> tint: Tint;
@fragment fn main(@location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(image, state, uv) * tint.value;
})";
        break;
    case rhi::ShaderStage::compute:
        request.source.text = R"(
struct Output { values: array<u32, 4> }
@group(1) @binding(9) var<storage, read_write> outputData: Output;
fn value(i: u32) -> u32 { return 12u + i; }
@compute @workgroup_size(2) fn main(@builtin(global_invocation_id) id: vec3u) {
    outputData.values[id.x] = value(id.x);
})";
        break;
    }
    return request;
}
} // namespace truffle::tests

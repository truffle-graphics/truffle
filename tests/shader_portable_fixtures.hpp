#pragma once

#include "truffle/shader/compiler.hpp"

namespace truffle::tests {

inline shader::CompileRequest portable_request(rhi::ShaderStage stage,
                                               rhi::ShaderTarget target,
                                               rhi::ShaderSourceLanguage language = rhi::ShaderSourceLanguage::glsl) {
    shader::CompileRequest request;
    request.name = "portable";
    request.stage = stage;
    request.target = target;
    request.language = language;
    request.allowExperimentalMetalGeneration = true;
    request.source.identity = "portable.glsl";
    request.source.text = language == rhi::ShaderSourceLanguage::glsl_es
        ? "#version 310 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 450\n";
    switch (stage) {
    case rhi::ShaderStage::vertex:
        request.source.text += R"(
layout(location=0) in vec2 position;
layout(location=0) out vec2 outgoingUV;
void main() { gl_Position=vec4(position,0.5,1); outgoingUV=position*0.5+0.5; }
)";
        break;
    case rhi::ShaderStage::fragment:
        request.source.text += R"(
layout(location=0) in vec2 incomingUV;
layout(location=0) out vec4 color;
layout(set=1,binding=5) uniform highp texture2D image;
layout(set=1,binding=6) uniform highp sampler state;
layout(set=1,binding=7,std140) uniform Tint { vec4 value; } tint;
void main() { color=texture(sampler2D(image,state),incomingUV)*tint.value; }
)";
        break;
    case rhi::ShaderStage::compute:
        request.source.text += R"(
layout(local_size_x=2) in;
layout(set=1,binding=9,std430) buffer Output { uint values[4]; } outputData;
layout(constant_id=3) const uint increment=7u;
layout(push_constant,std430) uniform Push { uint bias; } pushData;
void main() { uint i=gl_GlobalInvocationID.x; outputData.values[i]=pushData.bias+increment+i; }
)";
        break;
    }
    return request;
}

} // namespace truffle::tests

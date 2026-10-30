#include "shader_test_utils.hpp"
#include "truffle/shader/compiler.hpp"

#include <algorithm>
#include <bit>
#include <iostream>

#define TRUFFLE_CHECK(condition) TRUFFLE_SHADER_CHECK(condition)

namespace {
using namespace truffle;

shader::Compilation compile(shader::Compiler& compiler, std::string source,
                             rhi::ShaderStage stage = rhi::ShaderStage::compute,
                             shader::VulkanTarget target = {}) {
    shader::CompileRequest request;
    request.source = {"reflection.glsl", std::move(source)};
    request.stage = stage;
    request.vulkanTarget = target;
    return tests::require(compiler.compile(request));
}

void reject(shader::Compiler& compiler, const std::string& source,
            const char* diagnostic, rhi::ShaderStage stage = rhi::ShaderStage::compute) {
    shader::CompileRequest request;
    request.source = {"negative.glsl", source};
    request.stage = stage;
    const auto result = compiler.compile(request);
    TRUFFLE_CHECK(!result.ok());
    TRUFFLE_CHECK(result.status().code == rhi::StatusCode::unsupported);
    if (result.status().message.find(diagnostic) == std::string::npos)
        throw std::runtime_error(result.status().message);
}

void constants_and_resources(shader::Compiler& compiler) {
    auto result = compile(compiler, R"(#version 450
layout(local_size_x=4,local_size_y=2,local_size_z=3) in;
layout(constant_id=9) const uint unsignedValue=0xfedcba98u;
layout(constant_id=3) const int signedValue=-17;
layout(constant_id=2) const float floatValue=-1.25;
layout(constant_id=1) const bool enabled=true;
layout(constant_id=5) const bool disabled=false;
layout(push_constant,std430) uniform Push {
    layout(offset=16) uint value;
    layout(offset=32) mat2x3 matrix;
    layout(offset=80) float weights[2];
} pc;
layout(set=1,binding=4,std140) uniform Uniforms {
    layout(offset=16) mat2x3 matrix;
    layout(offset=64) float weights[2];
} uniforms[2];
layout(set=1,binding=5,std430,row_major) readonly buffer Input {
    layout(offset=16) mat2x3 matrix;
    layout(offset=48) vec2 values[2];
} inputs[3];
layout(set=2,binding=1,std430) buffer Output {
    layout(offset=16) uint values[];
} outputs[2];
layout(set=3,binding=2) uniform texture2D images[2][3];
layout(set=3,binding=3) uniform sampler states[4];
layout(set=3,binding=4,rgba32f) readonly uniform image2D readImage;
layout(set=3,binding=5,rgba32f) writeonly uniform image2D writeImage;
void main() {
    outputs[1].values[0]=unsignedValue+uint(signedValue)+uint(floatValue)+
        uint(enabled)+uint(disabled)+pc.value+uint(uniforms[1].weights[1])+
        uint(inputs[2].values[1].x);
}
)");
    const auto& reflection = result.package.desc().variants[0].reflection;
    TRUFFLE_CHECK(reflection.requiredWorkgroupSize == rhi::Extent3D(4, 2, 3));
    TRUFFLE_CHECK(reflection.preferredWorkgroupSize == rhi::Extent3D(4, 2, 3));
    TRUFFLE_CHECK(reflection.pushConstants ==
                  std::vector<rhi::PushConstantRange>({{rhi::ShaderStage::compute, 16, 72}}));
    const std::vector<rhi::ShaderSpecializationConstant> expected{
        {1, "enabled", rhi::ShaderValueType::boolean, 1},
        {2, "floatValue", rhi::ShaderValueType::float32, std::bit_cast<std::uint32_t>(-1.25f)},
        {3, "signedValue", rhi::ShaderValueType::sint32, std::bit_cast<std::uint32_t>(-17)},
        {5, "disabled", rhi::ShaderValueType::boolean, 0},
        {9, "unsignedValue", rhi::ShaderValueType::uint32, 0xfedcba98u}};
    TRUFFLE_CHECK(reflection.specializationConstants == expected);
    TRUFFLE_CHECK(reflection.bindings.size() == 7);
    const auto binding = [&](std::size_t index, std::uint32_t group, std::uint32_t slot,
                             rhi::ResourceBindingType type, std::uint32_t count,
                             std::size_t bytes, bool readOnly) {
        const auto& b = reflection.bindings[index];
        TRUFFLE_CHECK(b.stage == rhi::ShaderStage::compute && b.group == group && b.binding == slot);
        TRUFFLE_CHECK(b.type == type && b.arrayCount == count);
        TRUFFLE_CHECK(b.minimumSize == bytes && b.readOnly == readOnly);
    };
    binding(0, 1, 4, rhi::ResourceBindingType::buffer, 2, 84, true);
    binding(1, 1, 5, rhi::ResourceBindingType::buffer, 3, 64, true);
    binding(2, 2, 1, rhi::ResourceBindingType::buffer, 2, 20, false);
    binding(3, 3, 2, rhi::ResourceBindingType::texture, 6, 0, true);
    binding(4, 3, 3, rhi::ResourceBindingType::sampler, 4, 0, true);
    binding(5, 3, 4, rhi::ResourceBindingType::texture, 1, 0, true);
    binding(6, 3, 5, rhi::ResourceBindingType::texture, 1, 0, false);
    auto loaded = tests::require(rhi::ShaderPackage::load(result.package.bytes()));
    TRUFFLE_CHECK(loaded.bytes().size() == result.package.bytes().size());
    const auto& roundtrip = loaded.desc().variants[0].reflection;
    TRUFFLE_CHECK(roundtrip.pushConstants == reflection.pushConstants);
    TRUFFLE_CHECK(roundtrip.specializationConstants == expected);
    TRUFFLE_CHECK(roundtrip.bindings.size() == reflection.bindings.size());
    for (std::size_t i = 0; i < reflection.bindings.size(); ++i) {
        const auto& a = reflection.bindings[i];
        const auto& b = roundtrip.bindings[i];
        TRUFFLE_CHECK(a.name == b.name && a.arrayCount == b.arrayCount &&
                      a.minimumSize == b.minimumSize && a.readOnly == b.readOnly);
    }

    for (const auto target : shader::Compiler::routes().front().vulkanTargets) {
        auto legacy = compile(compiler, R"(#version 450
layout(local_size_x=1) in;
layout(constant_id=0) const uint N=2;
layout(push_constant) uniform Push { layout(offset=16) uint value; } pc;
layout(set=0,binding=0) readonly buffer Input { float values[2]; } data[2];
void main() {}
)", rhi::ShaderStage::compute, target);
        const auto& r = legacy.package.desc().variants[0].reflection;
        TRUFFLE_CHECK(r.bindings[0].minimumSize == 8 && r.bindings[0].arrayCount == 2 &&
                      r.bindings[0].readOnly);
        TRUFFLE_CHECK(r.pushConstants ==
                      std::vector<rhi::PushConstantRange>({{rhi::ShaderStage::compute, 16, 4}}));
        TRUFFLE_CHECK(r.specializationConstants ==
                      std::vector<rhi::ShaderSpecializationConstant>({{0, "N", rhi::ShaderValueType::uint32, 2}}));
    }
    auto nested = compile(compiler, R"(#version 450
layout(local_size_x=1) in;
struct Value { mat2 matrix; vec3 vector; };
layout(set=0,binding=0,std140) uniform Data { layout(offset=16) Value values[2]; } data;
void main() {}
)");
    TRUFFLE_CHECK(nested.package.desc().variants[0].reflection.bindings[0].minimumSize == 108);
}

void interfaces(shader::Compiler& compiler) {
    auto vertex = compile(compiler, R"(#version 450
layout(location=0) in mat2x3 transforms[2];
layout(location=4,component=0) in vec2 uv;
layout(location=5) in mat2 basis;
layout(location=0) out Output { centroid mat2x3 basis; centroid vec2 uv[2]; } outputs;
void main() { gl_Position=vec4(transforms[0][0],1); outputs.basis=transforms[0];
              outputs.uv[0]=uv; outputs.uv[1]=basis[0]; }
)", rhi::ShaderStage::vertex);
    const auto& edges = vertex.package.desc().variants[0].reflection.interfaces;
    TRUFFLE_CHECK(edges.size() == 7 && vertex.interfaces.size() == 11);
    for (std::uint32_t i = 0; i < edges.size(); ++i) {
        TRUFFLE_CHECK(edges[i].stage == rhi::ShaderStage::vertex && edges[i].location == i);
        TRUFFLE_CHECK(edges[i].type == (i < 4 ? "vec3" : "vec2"));
        TRUFFLE_CHECK(edges[i].interpolation == "smooth");
    }
    TRUFFLE_CHECK(edges[0].name == "transforms[0][0]");
    TRUFFLE_CHECK(edges[3].name == "transforms[1][1]");
    TRUFFLE_CHECK(edges[4].name == "uv");
    TRUFFLE_CHECK(edges[6].name == "basis[1]");
    for (const auto& value : vertex.interfaces)
        if (!value.input) TRUFFLE_CHECK(value.variable.interpolation == "centroid smooth");
    auto loaded = tests::require(rhi::ShaderPackage::load(vertex.package.bytes()));
    TRUFFLE_CHECK(loaded.desc().variants[0].reflection.interfaces == edges);

    auto fragment = compile(compiler, R"(#version 450
layout(location=0) centroid in vec3 renamed[2];
layout(location=2) centroid in mat2 renamedMatrix;
layout(location=0,index=0) out vec4 color;
void main() { color=vec4(renamed[0]+vec3(renamedMatrix[0],0),1); }
)", rhi::ShaderStage::fragment);
    for (const auto& input : fragment.interfaces) {
        if (!input.input) continue;
        const auto found = std::find_if(vertex.interfaces.begin(), vertex.interfaces.end(),
            [&](const auto& output) { return !output.input &&
                output.variable.location == input.variable.location; });
        TRUFFLE_CHECK(found != vertex.interfaces.end());
        TRUFFLE_CHECK(found->variable.type == input.variable.type);
        TRUFFLE_CHECK(found->variable.interpolation == input.variable.interpolation);
    }
    auto members = compile(compiler, R"(#version 450
in Input { layout(location=3) flat ivec2 id; layout(location=7) noperspective vec2 uv; } values;
layout(location=0) out vec4 color;
void main() { color=vec4(values.id,values.uv); }
)", rhi::ShaderStage::fragment);
    TRUFFLE_CHECK(members.interfaces[1].input && members.interfaces[1].variable.location == 3);
    TRUFFLE_CHECK(members.interfaces[1].variable.type == "ivec2");
    TRUFFLE_CHECK(members.interfaces[1].variable.interpolation == "flat");
    TRUFFLE_CHECK(members.interfaces[2].variable.location == 7);
    TRUFFLE_CHECK(members.interfaces[2].variable.interpolation == "noperspective");
    auto nested = compile(compiler, R"(#version 450
struct Nested { mat2x3 basis; vec2 tail[2]; };
layout(location=2) in Block { sample Nested values[2]; } data;
layout(location=0) out vec4 color;
void main() { color=vec4(data.values[1].basis[1]+vec3(data.values[0].tail[0],0),1); }
)", rhi::ShaderStage::fragment);
    TRUFFLE_CHECK(nested.interfaces.size() == 9);
    for (std::size_t i = 1; i < nested.interfaces.size(); ++i) {
        const auto& leaf = nested.interfaces[i];
        TRUFFLE_CHECK(leaf.input && leaf.variable.location == i + 1);
        TRUFFLE_CHECK(leaf.variable.type == ((i - 1) % 4 < 2 ? "vec3" : "vec2"));
        TRUFFLE_CHECK(leaf.variable.interpolation == "sample smooth");
    }
    TRUFFLE_CHECK(nested.interfaces[6].variable.name == "data.values[1].basis[1]");
}

void negative(shader::Compiler& compiler) {
    reject(compiler, R"(#version 450
layout(local_size_x=4,local_size_x_id=7) in;
void main() {}
)", "per-axis specialization IDs");
    reject(compiler, R"(#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(local_size_x=1) in;
layout(constant_id=0) const int64_t value=123;
void main() {}
)", "32-bit scalar");
    reject(compiler, R"(#version 450
layout(local_size_x=1) in;
layout(constant_id=0) const uint count=2;
layout(set=0,binding=0) buffer Data { uint value; } data[count];
void main() {}
)", "fixed literal dimensions");
    reject(compiler, R"(#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x=1) in;
layout(set=0,binding=0) buffer Data { uint value; } data[];
void main() { data[nonuniformEXT(gl_GlobalInvocationID.x)].value=1; }
)", "unsized descriptor");
    reject(compiler, R"(#version 450
layout(location=0,component=1) in vec2 packed;
layout(location=0) out vec4 color;
void main() { color=vec4(packed,0,1); }
)", "Component/index", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
#extension GL_EXT_fragment_shader_barycentric : require
layout(location=0) pervertexEXT in vec3 values[];
layout(location=0) out vec4 color;
void main() { color=vec4(values[0],1); }
)", "Extended per-vertex", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
layout(location=0,index=1) out vec4 color;
void main() { color=vec4(1); }
)", "Component/index", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
layout(location=0) in dvec3 value;
void main() { gl_Position=vec4(value,1); }
)", "32-bit scalar/vector", rhi::ShaderStage::vertex);
    reject(compiler, R"(#version 450
layout(set=0,binding=0) uniform sampler2D combined;
layout(location=0) out vec4 color;
void main() { color=texture(combined,vec2(0)); }
)", "combined sampler", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput attachment;
layout(location=0) out vec4 color;
void main() { color=subpassLoad(attachment); }
)", "subpass input", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
layout(set=0,binding=0) uniform textureBuffer bufferTexture;
layout(location=0) out vec4 color;
void main() { color=vec4(1); }
)", "texel buffer", rhi::ShaderStage::fragment);
    reject(compiler, R"(#version 450
#extension GL_EXT_ray_query : require
layout(local_size_x=1) in;
layout(set=0,binding=0) uniform accelerationStructureEXT scene;
void main() {}
)", "unsupported type");
    reject(compiler, R"(#version 450
layout(local_size_x=1) in;
layout(constant_id=3) const uint count=2;
layout(set=0,binding=0) buffer Data { uint values[count]; } data;
void main() {}
)", "minimum size");
}
}

int main() {
    try {
        if (!truffle::shader::Compiler::available()) return 0;
        truffle::shader::Compiler compiler;
        constants_and_resources(compiler);
        interfaces(compiler);
        negative(compiler);
    } catch (const std::exception& error) {
        std::cerr << "Reflection: " << error.what() << '\n';
        return 1;
    }
}

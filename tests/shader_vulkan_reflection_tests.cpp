#include "shader_test_utils.hpp"
#include "truffle/shader/compiler.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

#include <array>
#include <bit>
#include <iostream>

namespace {
using namespace truffle;

void specialized_workgroups(rhi::Device& device, shader::Compiler& compiler) {
    shader::CompileRequest request;
    request.stage = rhi::ShaderStage::compute;
    constexpr std::string_view body = R"(
layout(set=0,binding=0,std430) buffer Output { uint values[32]; } outputData;
void main() {
    uint count=gl_WorkGroupSize.x*gl_WorkGroupSize.y*gl_WorkGroupSize.z;
    uint index=gl_WorkGroupID.x*count+gl_LocalInvocationIndex;
    outputData.values[index]=gl_WorkGroupSize.x*100u+gl_WorkGroupSize.y*1000u+
        gl_WorkGroupSize.z*10000u+index;
}
)";
    request.source = {"workgroups.comp", "#version 450\n"
        "layout(local_size_x=2,local_size_y=1,local_size_z=2,local_size_x_id=0,local_size_y_id=7) in;\n" +
        std::string{body}};
    auto compiled = tests::require(compiler.compile(request));
    auto package = tests::require(rhi::ShaderPackage::load(compiled.package.bytes()));
    const auto& reflection = package.desc().variants[0].reflection;
    TRUFFLE_SHADER_CHECK(reflection.workgroupSpecializationIds ==
                        rhi::WorkgroupSpecializationIds({0, 7, std::nullopt}));
    TRUFFLE_SHADER_CHECK(reflection.requiredWorkgroupSize == rhi::Extent3D(2, 1, 2));
    TRUFFLE_SHADER_CHECK(reflection.bindings[0].minimumSize == 128);
    auto shader = tests::require(device.create_shader(package, rhi::ShaderTarget::spirv,
                                                      "main", rhi::ShaderStage::compute));
    auto sharedRequest = request;
    sharedRequest.source = {"shared-workgroups.comp", "#version 450\n"
        "layout(local_size_x=1,local_size_y=1,local_size_z=2,local_size_x_id=0,local_size_y_id=0) in;\n" +
        std::string{body}};
    auto shared = tests::require(compiler.compile(sharedRequest));
    auto sharedPackage = tests::require(rhi::ShaderPackage::load(shared.package.bytes()));
    TRUFFLE_SHADER_CHECK(sharedPackage.desc().variants[0].reflection.specializationConstants.size() == 1);
    auto sharedShader = tests::require(device.create_shader(sharedPackage, rhi::ShaderTarget::spirv,
                                                            "main", rhi::ShaderStage::compute));
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 128}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    for (std::size_t pass = 0; pass < 3; ++pass) {
        const auto dimensions = pass == 0 ? rhi::Extent3D{2, 1, 2} :
                                pass == 1 ? rhi::Extent3D{3, 2, 2} : rhi::Extent3D{2, 2, 2};
        const std::vector<rhi::SpecializationValue> values = pass == 0 ? std::vector<rhi::SpecializationValue>{} :
            pass == 1 ? std::vector<rhi::SpecializationValue>{{0, rhi::ShaderValueType::uint32, 3},
                                                            {7, rhi::ShaderValueType::uint32, 2}}
                      : std::vector<rhi::SpecializationValue>{{0, rhi::ShaderValueType::uint32, 2}};
        auto pipeline = tests::require(device.create_compute_pipeline({
            .computeShader = pass == 2 ? &sharedShader : &shader,
            .layout = &layout, .requiredWorkgroupSize = dimensions, .specializationConstants = values}));
        TRUFFLE_SHADER_CHECK(pipeline.preferred_workgroup_size() == dimensions);
        auto output = tests::require(device.create_buffer({
            .size = 128, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
        std::array<std::uint32_t, 32> actual;
        actual.fill(0xdeadbeefu);
        tests::require(output.write(0, std::as_bytes(std::span{actual})));
        auto group = tests::require(device.create_bind_group({
            .layout = &groupLayout, .arena = &arena, .entries = {{.binding = 0, .buffer = &output, .size = 128}}}));
        auto list = tests::require(pool.allocate());
        tests::require(list.begin());
        auto encoder = tests::require(list.begin_compute());
        tests::require(encoder.bind_pipeline(pipeline));
        tests::require(encoder.bind_group(0, group));
        tests::require(encoder.dispatch(2, 1, 1));
        tests::require(encoder.end());
        tests::require(list.end());
        const std::array<rhi::CommandList*, 1> lists{&list};
        tests::require(queue.submit(lists));
        tests::require(output.read(0, std::as_writable_bytes(std::span{actual})));
        const auto written = 2 * dimensions.width * dimensions.height * dimensions.depth;
        const auto base = dimensions.width * 100 + dimensions.height * 1000 + dimensions.depth * 10000;
        for (std::size_t index = 0; index < actual.size(); ++index)
            TRUFFLE_SHADER_CHECK(actual[index] == (index < written ? base + index : 0xdeadbeefu));
    }
    TRUFFLE_SHADER_CHECK(!device.create_compute_pipeline({
        .computeShader = &shader, .layout = &layout,
        .requiredWorkgroupSize = {2, 1, 2},
        .specializationConstants = {{0, rhi::ShaderValueType::uint32, 3}}}).ok());
    const auto outOfLimits = device.create_compute_pipeline({
        .computeShader = &shader, .layout = &layout,
        .specializationConstants = {{0, rhi::ShaderValueType::uint32, 0xffffffffu}}});
    TRUFFLE_SHADER_CHECK(!outOfLimits.ok() && outOfLimits.status().code == rhi::StatusCode::unsupported);
    std::cout << "specialized workgroups: 2x1x2 -> 3x2x2 + shared-ID 2x2x2; "
                 "exact 8/24/16 invocations and untouched tail passed\n";
}

int run() {
    auto created = rhi::create_vulkan_instance({.enableValidation = true});
    if (!created.ok()) {
        std::cout << "Vulkan unavailable: " << created.status().message << '\n';
        return 77;
    }
    auto instance = std::move(created).value();
    const auto validation = rhi::detail::last_vulkan_validation_state_for_testing();
    TRUFFLE_SHADER_CHECK(validation != rhi::detail::VulkanValidationState::failed_to_load);
    auto adapter = tests::require(instance.adapter(0));
    TRUFFLE_SHADER_CHECK(adapter.info().native);
    std::cout << "adapter: " << adapter.info().name << "; validation "
              << (validation == rhi::detail::VulkanValidationState::active ? "active" : "not installed") << '\n';
    auto device = tests::require(adapter.request_device());
    shader::CompileRequest request;
    request.stage = rhi::ShaderStage::compute;
    request.source = {"constants-arrays.comp", R"(#version 450
layout(local_size_x=4) in;
layout(constant_id=1) const uint unsignedValue=7;
layout(constant_id=2) const int signedValue=-2;
layout(constant_id=3) const float floatValue=1.5;
layout(constant_id=4) const bool enabled=true;
layout(push_constant,std430) uniform Push {
    layout(offset=16) uint base;
    layout(offset=32) mat2x3 matrix;
} pc;
layout(set=0,binding=0,std430) buffer Output { uint values[4]; } outputs[2];
layout(set=0,binding=1,std140) uniform Uniforms { uint value; } uniforms[2];
layout(set=0,binding=2,std430) readonly buffer Input { uint value; } inputs[2];
layout(set=0,binding=3) uniform texture2D images[2];
layout(set=0,binding=4) uniform sampler states[2];
void main() {
    uint i=gl_LocalInvocationID.x;
    uint value=pc.base+uint(pc.matrix[0].x*pc.matrix[1].y+pc.matrix[1].z)+unsignedValue+
        uint(signedValue+10)+uint(floatValue*2.0)+(enabled?100u:200u);
    uint first=uint(round(textureLod(sampler2D(images[0],states[1]),vec2(0.5),0).r*255.0));
    uint second=uint(round(textureLod(sampler2D(images[1],states[0]),vec2(0.5),0).r*255.0));
    outputs[0].values[i]=value+uniforms[0].value+inputs[1].value+i+first;
    outputs[1].values[i]=value+uniforms[1].value+inputs[0].value+i+second;
}
)"};
    shader::Compiler compiler;
    auto compiled = tests::require(compiler.compile(request));
    auto package = tests::require(rhi::ShaderPackage::load(compiled.package.bytes()));
    const auto& reflection = package.desc().variants[0].reflection;
    TRUFFLE_SHADER_CHECK(reflection.pushConstants ==
        std::vector<rhi::PushConstantRange>({{rhi::ShaderStage::compute, 16, 44}}));
    TRUFFLE_SHADER_CHECK(reflection.specializationConstants.size() == 4);
    TRUFFLE_SHADER_CHECK(reflection.bindings.size() == 5);
    for (const auto& binding : reflection.bindings)
        TRUFFLE_SHADER_CHECK(binding.arrayCount == 2);
    TRUFFLE_SHADER_CHECK(reflection.bindings[0].minimumSize == 16 &&
                        !reflection.bindings[0].readOnly);
    TRUFFLE_SHADER_CHECK(reflection.bindings[1].minimumSize == 4 &&
                        reflection.bindings[1].readOnly);
    TRUFFLE_SHADER_CHECK(reflection.bindings[2].minimumSize == 4 &&
                        reflection.bindings[2].readOnly);
    auto shader = tests::require(device.create_shader(package, rhi::ShaderTarget::spirv,
                                                      "main", rhi::ShaderStage::compute));
    rhi::BindGroupLayoutDesc bindings;
    for (const auto& b : reflection.bindings) {
        const auto type = b.type == rhi::ResourceBindingType::texture ? rhi::BindingType::sampled_texture
            : b.type == rhi::ResourceBindingType::sampler ? rhi::BindingType::sampler
            : b.binding == 1 ? rhi::BindingType::uniform_buffer : rhi::BindingType::storage_buffer;
        bindings.entries.push_back({
            .binding = b.binding, .type = type,
            .arrayCount = b.arrayCount, .visibility = rhi::ShaderStageMask::compute,
            .minimumBufferSize = b.minimumSize});
    }
    auto groupLayout = tests::require(device.create_bind_group_layout(bindings));
    auto layout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&groupLayout}, .pushConstants = reflection.pushConstants}));
    auto defaults = tests::require(device.create_compute_pipeline({
        .computeShader = &shader, .layout = &layout}));
    const std::vector<rhi::SpecializationValue> overrides{
        {1, rhi::ShaderValueType::uint32, 13},
        {2, rhi::ShaderValueType::sint32, std::bit_cast<std::uint32_t>(-4)},
        {3, rhi::ShaderValueType::float32, std::bit_cast<std::uint32_t>(2.5f)},
        {4, rhi::ShaderValueType::boolean, 0}};
    auto specialized = tests::require(device.create_compute_pipeline({
        .computeShader = &shader, .layout = &layout, .specializationConstants = overrides}));
    TRUFFLE_SHADER_CHECK(!device.create_compute_pipeline({
        .computeShader = &shader, .layout = &layout,
        .specializationConstants = {{1, rhi::ShaderValueType::float32, 0}}}).ok());
    auto tooShortLayout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&groupLayout},
        .pushConstants = {{rhi::ShaderStage::compute, 16, 40}}}));
    TRUFFLE_SHADER_CHECK(!device.create_compute_pipeline({
        .computeShader = &shader, .layout = &tooShortLayout}).ok());

    std::array<rhi::Buffer, 2> output, uniform, input;
    std::array<rhi::Texture, 2> images;
    std::array<rhi::TextureView, 2> views;
    std::array<rhi::Sampler, 2> samplers;
    for (std::size_t i = 0; i < 2; ++i) {
        output[i] = tests::require(device.create_buffer({
            .size = 16, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
        uniform[i] = tests::require(device.create_buffer({
            .size = 4, .usage = rhi::BufferUsage::uniform, .memory = rhi::MemoryDomain::upload}));
        input[i] = tests::require(device.create_buffer({
            .size = 4, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::upload}));
        const std::array<std::uint32_t, 1> u{static_cast<std::uint32_t>(10 + i * 10)};
        const std::array<std::uint32_t, 1> in{static_cast<std::uint32_t>(30 + i * 50)};
        tests::require(uniform[i].write(0, std::as_bytes(std::span{u})));
        tests::require(input[i].write(0, std::as_bytes(std::span{in})));
    }
    auto uploadPool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto uploadQueue = tests::require(device.queue(rhi::QueueKind::graphics));
    for (std::size_t i = 0; i < images.size(); ++i) {
        images[i] = tests::require(device.create_texture({
            .extent = {1, 1, 1}, .format = rhi::TextureFormat::rgba8_unorm,
            .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_destination}));
        auto upload = tests::require(device.create_buffer({
            .size = 4, .usage = rhi::BufferUsage::copy_source, .memory = rhi::MemoryDomain::upload}));
        const std::array pixel{std::byte{static_cast<unsigned char>(i == 0 ? 20 : 31)},
                               std::byte{0}, std::byte{0}, std::byte{255}};
        tests::require(upload.write(0, pixel));
        auto list = tests::require(uploadPool.allocate());
        tests::require(list.begin());
        auto copy = tests::require(list.begin_copy());
        tests::require(copy.copy_buffer_to_texture(upload, images[i], {
            .texture = {.extent = {1, 1, 1}}}));
        tests::require(copy.end());
        tests::require(list.end());
        const std::array<rhi::CommandList*, 1> lists{&list};
        tests::require(uploadQueue.submit(lists));
        views[i] = tests::require(device.create_texture_view(images[i]));
        samplers[i] = tests::require(device.create_sampler({}));
    }
    auto arena = tests::require(device.create_descriptor_arena());
    rhi::BindGroupDesc desc{.layout = &groupLayout, .arena = &arena};
    for (std::uint32_t i = 0; i < 2; ++i) {
        desc.entries.push_back({.binding = 0, .arrayElement = i, .buffer = &output[i], .size = 16});
        desc.entries.push_back({.binding = 1, .arrayElement = i, .buffer = &uniform[i], .size = 4});
        desc.entries.push_back({.binding = 2, .arrayElement = i, .buffer = &input[i], .size = 4});
        desc.entries.push_back({.binding = 3, .arrayElement = i, .textureView = &views[i]});
        desc.entries.push_back({.binding = 4, .arrayElement = i, .sampler = &samplers[i]});
    }
    auto group = tests::require(device.create_bind_group(desc));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    for (int pass = 0; pass < 2; ++pass) {
        auto list = tests::require(pool.allocate());
        tests::require(list.begin());
        auto encoder = tests::require(list.begin_compute());
        tests::require(encoder.bind_pipeline(pass == 0 ? defaults : specialized));
        tests::require(encoder.bind_group(0, group));
        // Matrix columns start at 32 and 48; the last scalar ends at 60.
        const std::array<std::uint32_t, 11> push{
            5, 0, 0, 0, std::bit_cast<std::uint32_t>(2.0f), 0, 0, 0,
            0, std::bit_cast<std::uint32_t>(3.0f), std::bit_cast<std::uint32_t>(7.0f)};
        tests::require(encoder.push_constants(16, std::as_bytes(std::span{push})));
        tests::require(encoder.dispatch(1, 1, 1));
        tests::require(encoder.end());
        tests::require(list.end());
        const std::array<rhi::CommandList*, 1> lists{&list};
        tests::require(queue.submit(lists));
        for (std::size_t index = 0; index < output.size(); ++index) {
            std::array<std::uint32_t, 4> actual{};
            tests::require(output[index].read(0, std::as_writable_bytes(std::span{actual})));
            const auto base = (pass == 0 ? 186u : 292u) + (index == 0 ? 60u : 31u);
            const std::array expected{base, base + 1, base + 2, base + 3};
            TRUFFLE_SHADER_CHECK(actual == expected);
        }
    }
    std::cout << "exact scalar defaults/overrides + offset push + buffer/image/sampler-array readback passed\n";
    specialized_workgroups(device, compiler);
    return 0;
}
}

int main() {
    try { return run(); }
    catch (const std::exception& error) {
        std::cerr << "Vulkan reflection: " << error.what() << '\n';
        return 1;
    }
}

#include "shader_hlsl_fixtures.hpp"
#include "renderer_native_test_utils.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"
#if defined(TRUFFLE_HLSL_TEST_METAL)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TRUFFLE_HLSL_TEST_OPENGL)
#include "truffle/rhi/opengl_backend.hpp"
#endif

#include <iostream>

namespace {
using namespace truffle;

rhi::Shader load(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderStage stage,
                 rhi::ShaderTarget target) {
    auto request = tests::hlsl_request(stage, target);
    request.allowExperimentalMetalGeneration = true;
    const auto result = tests::require(compiler.compile(request));
    const auto package = tests::require(rhi::ShaderPackage::load(
        result.package.bytes(), {.allowExperimentalMetalGeneration = true}));
    return tests::require(device.create_shader(package, request.target, request.entryPoint, stage,
        package.desc().variants.front().permutation));
}

void graphics(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto vertex = load(device, compiler, rhi::ShaderStage::vertex, target);
    auto fragment = load(device, compiler, rhi::ShaderStage::fragment, target);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {{.binding = 7, .type = rhi::BindingType::uniform_buffer,
            .visibility = rhi::ShaderStageMask::fragment, .minimumBufferSize = 16}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto pipeline = tests::require(device.create_pipeline({
        .vertexShader = &vertex, .fragmentShader = &fragment, .layout = &layout,
        .vertexBuffers = {{.stride = 8, .attributes = {{.location = 0, .format = rhi::VertexFormat::float32x2}}}},
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}}}));
    const std::array<float, 6> positions{-1, -1, 3, -1, -1, 3};
    auto vertices = tests::require(device.create_buffer({
        .size = sizeof(positions), .usage = rhi::BufferUsage::vertex, .memory = rhi::MemoryDomain::upload}));
    tests::require(vertices.write(0, std::as_bytes(std::span{positions})));
    auto uniform = tests::require(device.create_buffer({
        .size = 16, .usage = rhi::BufferUsage::uniform, .memory = rhi::MemoryDomain::upload}));
    const std::array<float, 4> tint{0.25F, 0.5F, 1, 1};
    tests::require(uniform.write(0, std::as_bytes(std::span{tint})));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena, .entries = {{.binding = 7, .buffer = &uniform, .size = 16}}}));
    auto output = tests::make_color_target(device);
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto render = tests::require(list.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &output, .clear = {0, 0, 0, 1}}}}));
    tests::require(render.bind_pipeline(pipeline));
    tests::require(render.bind_group(1, group));
    tests::require(render.bind_vertex_buffer(0, vertices));
    tests::require(render.draw(3));
    tests::require(render.end());
    tests::require(list.end());
    tests::submit_graphics(device, list);
    tests::check_color(device, output, {std::byte{64}, std::byte{128}, std::byte{255}, std::byte{255}},
        ("HLSL " + std::string{rhi::shader_target_name(target)}).c_str());
}

void compute(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto shader = load(device, compiler, rhi::ShaderStage::compute, target);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {
            {.binding = 7, .type = rhi::BindingType::uniform_buffer, .visibility = rhi::ShaderStageMask::compute,
             .minimumBufferSize = 16},
            {.binding = 9, .type = rhi::BindingType::storage_buffer, .visibility = rhi::ShaderStageMask::compute,
             .minimumBufferSize = 4}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto pipeline = tests::require(device.create_compute_pipeline({.computeShader = &shader, .layout = &layout}));
    auto uniform = tests::require(device.create_buffer({
        .size = 16, .usage = rhi::BufferUsage::uniform, .memory = rhi::MemoryDomain::upload}));
    const std::array bias{100u, 0u, 0u, 0u};
    tests::require(uniform.write(0, std::as_bytes(std::span{bias})));
    auto buffer = tests::require(device.create_buffer({
        .size = 32, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
    std::array<std::uint32_t, 8> actual;
    actual.fill(0xdeadbeefu);
    tests::require(buffer.write(0, std::as_bytes(std::span{actual})));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena,
        .entries = {{.binding = 7, .buffer = &uniform, .size = 16}, {.binding = 9, .buffer = &buffer, .size = 32}}}));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto encoder = tests::require(list.begin_compute());
    tests::require(encoder.bind_pipeline(pipeline));
    tests::require(encoder.bind_group(1, group));
    tests::require(encoder.dispatch(2, 1, 1));
    tests::require(encoder.end());
    tests::require(list.end());
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    const std::array<rhi::CommandList*, 1> lists{&list};
    tests::require(queue.submit(lists));
    tests::require(buffer.read(0, std::as_writable_bytes(std::span{actual})));
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != (i < 4 ? 107u + i : 0xdeadbeefu))
            throw std::runtime_error("HLSL compute output/sentinel mismatch");
    std::cout << "HLSL " << rhi::shader_target_name(target)
              << ": exact compute 107..110, four untouched sentinel values\n";
}
} // namespace

int main() {
    try {
        shader::Compiler compiler;
        std::vector targets{rhi::ShaderTarget::spirv};
#if defined(TRUFFLE_HLSL_TEST_METAL)
        targets.push_back(rhi::ShaderTarget::metal_source);
#endif
#if defined(TRUFFLE_HLSL_TEST_OPENGL)
        targets.push_back(rhi::ShaderTarget::glsl);
#endif
        for (const auto target : targets) {
            const auto create = [&]() -> rhi::Result<rhi::Instance> {
#if defined(TRUFFLE_HLSL_TEST_METAL)
                if (target == rhi::ShaderTarget::metal_source)
                    return rhi::create_metal_instance({.enableValidation = true});
#endif
#if defined(TRUFFLE_HLSL_TEST_OPENGL)
                if (target == rhi::ShaderTarget::glsl)
                    return rhi::create_opengl_instance({.enableValidation = true});
#endif
                return rhi::create_vulkan_instance({.enableValidation = true});
            };
            auto instance = tests::require(create());
            if (target == rhi::ShaderTarget::spirv &&
                rhi::detail::last_vulkan_validation_state_for_testing() != rhi::detail::VulkanValidationState::active)
                throw std::runtime_error("HLSL native evidence requires active Vulkan validation");
            auto adapter = tests::require(instance.adapter(0));
            auto device = tests::require(adapter.request_device());
            std::cout << "HLSL native adapter: " << adapter.info().name << '\n';
            graphics(device, compiler, target);
            if (target != rhi::ShaderTarget::glsl) compute(device, compiler, target);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

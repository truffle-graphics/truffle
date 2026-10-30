#include "shader_portable_fixtures.hpp"
#include "renderer_native_test_utils.hpp"
#include "truffle/rhi/metal_backend.hpp"
#include "truffle/rhi/opengl_backend.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

#include <iostream>

namespace {
using namespace truffle;

rhi::Shader load_shader(rhi::Device& device, shader::Compiler& compiler,
                        rhi::ShaderTarget target, rhi::ShaderStage stage) {
    auto compilation = tests::require(compiler.compile(tests::portable_request(stage, target)));
    auto package = tests::require(rhi::ShaderPackage::load(compilation.package.bytes(),
        {.allowExperimentalMetalGeneration = true}));
    return tests::require(device.create_shader(package, target, "main", stage));
}

void graphics(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto vertex = load_shader(device, compiler, target, rhi::ShaderStage::vertex);
    auto fragment = load_shader(device, compiler, target, rhi::ShaderStage::fragment);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1,
        .entries = {
            {.binding = 5, .type = rhi::BindingType::sampled_texture, .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 6, .type = rhi::BindingType::sampler, .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 7, .type = rhi::BindingType::uniform_buffer, .visibility = rhi::ShaderStageMask::fragment,
             .minimumBufferSize = 16}}}));
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
    const std::array<float, 4> tint{0.5F, 1, 1, 1};
    tests::require(uniform.write(0, std::as_bytes(std::span{tint})));
    auto image = tests::require(device.create_texture({
        .extent = {1, 1, 1}, .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::sampled | rhi::TextureUsage::copy_destination}));
    const std::array pixel{std::byte{128}, std::byte{64}, std::byte{32}, std::byte{255}};
    auto upload = tests::require(device.create_buffer({
        .size = 4, .usage = rhi::BufferUsage::copy_source, .memory = rhi::MemoryDomain::upload}));
    tests::require(upload.write(0, pixel));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_buffer_to_texture(upload, image, {.texture = {.extent = {1, 1, 1}}}));
    tests::require(copy.end());
    tests::require(list.end());
    tests::submit_graphics(device, list);
    auto view = tests::require(device.create_texture_view(image));
    auto sampler = tests::require(device.create_sampler({}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena,
        .entries = {{.binding = 5, .textureView = &view}, {.binding = 6, .sampler = &sampler},
                    {.binding = 7, .buffer = &uniform, .size = 16}}}));
    auto output = tests::make_color_target(device);
    auto renderList = tests::require(pool.allocate());
    tests::require(renderList.begin());
    auto render = tests::require(renderList.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &output, .clear = {0, 0, 0, 1}}}}));
    tests::require(render.bind_pipeline(pipeline));
    tests::require(render.bind_vertex_buffer(0, vertices));
    tests::require(render.bind_group(1, group));
    tests::require(render.draw(3));
    tests::require(render.end());
    tests::require(renderList.end());
    tests::submit_graphics(device, renderList);
    tests::check_color(device, output, {std::byte{64}, std::byte{64}, std::byte{32}, std::byte{255}},
                       std::string{rhi::shader_target_name(target)}.c_str());
}

void compute(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto request = tests::portable_request(rhi::ShaderStage::compute, target);
    request.source.text.replace(request.source.text.find("local_size_x=2"), 14,
                                "local_size_x=2,local_size_x_id=0");
    request.source.text.replace(request.source.text.find("values[4]"), 9, "values[8]");
    request.source.text.replace(request.source.text.find("pushData.bias+increment+i"),
        std::string{"pushData.bias+increment+i"}.size(), "pushData.bias+increment+i+gl_WorkGroupSize.x*1000u");
    const auto compilation = tests::require(compiler.compile(request));
    const auto package = tests::require(rhi::ShaderPackage::load(compilation.package.bytes(),
        {.allowExperimentalMetalGeneration = true}));
    auto shader = tests::require(device.create_shader(package, target, "main", rhi::ShaderStage::compute));
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {{.binding = 9, .type = rhi::BindingType::storage_buffer,
                               .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 32}}}));
    auto layout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&groupLayout}, .pushConstants = {{rhi::ShaderStage::compute, 0, 4}}}));
    auto buffer = tests::require(device.create_buffer({
        .size = 32, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena, .entries = {{.binding = 9, .buffer = &buffer, .size = 32}}}));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    for (const auto increment : {7u, 13u}) {
        const auto width = increment == 7 ? 2u : 3u;
        std::array<std::uint32_t, 8> actual;
        actual.fill(0xdeadbeefu);
        tests::require(buffer.write(0, std::as_bytes(std::span{actual})));
        auto pipeline = tests::require(device.create_compute_pipeline({
            .computeShader = &shader, .layout = &layout,
            .specializationConstants = {{0, rhi::ShaderValueType::uint32, width},
                                        {3, rhi::ShaderValueType::uint32, increment}}}));
        auto list = tests::require(pool.allocate());
        tests::require(list.begin());
        auto encoder = tests::require(list.begin_compute());
        tests::require(encoder.bind_pipeline(pipeline));
        tests::require(encoder.bind_group(1, group));
        const std::array bias{100u};
        tests::require(encoder.push_constants(0, std::as_bytes(std::span{bias})));
        tests::require(encoder.dispatch(2, 1, 1));
        tests::require(encoder.end());
        tests::require(list.end());
        const std::array<rhi::CommandList*, 1> lists{&list};
        tests::require(queue.submit(lists));
        tests::require(buffer.read(0, std::as_writable_bytes(std::span{actual})));
        for (std::size_t i = 0; i < actual.size(); ++i)
            if (actual[i] != (i < width * 2 ? width * 1000 + 100 + increment + i : 0xdeadbeefu))
                throw std::runtime_error("generated compute mismatch");
    }
    std::cout << rhi::shader_target_name(target) << ": exact compute 2107..2110 / 3113..3118, sentinel tails\n";
}

int run() {
    shader::Compiler compiler;
    for (const auto target : {rhi::ShaderTarget::spirv, rhi::ShaderTarget::metal_source, rhi::ShaderTarget::glsl}) {
        auto instance = tests::require(target == rhi::ShaderTarget::spirv
            ? rhi::create_vulkan_instance({.enableValidation = true}) :
            target == rhi::ShaderTarget::metal_source ? rhi::create_metal_instance({.enableValidation = true}) :
                                                      rhi::create_opengl_instance({.enableValidation = true}));
        auto adapter = tests::require(instance.adapter(0));
        if (target == rhi::ShaderTarget::spirv) {
            if (rhi::detail::last_vulkan_validation_state_for_testing() !=
                rhi::detail::VulkanValidationState::active)
                throw std::runtime_error("generated portable evidence requires active Vulkan validation");
            std::cout << "Vulkan validation active\n";
        }
        std::cout << "native adapter: " << adapter.info().name << '\n';
        auto device = tests::require(adapter.request_device());
        graphics(device, compiler, target);
        if (target != rhi::ShaderTarget::glsl) compute(device, compiler, target);
    }
    return 0;
}
}

int main() {
    try { return run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

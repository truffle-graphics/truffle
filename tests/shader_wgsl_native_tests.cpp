#include "shader_wgsl_fixtures.hpp"
#include "renderer_native_test_utils.hpp"
#include "truffle/rhi/metal_backend.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

namespace {
using namespace truffle;

rhi::Shader load(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderStage stage,
                 rhi::ShaderTarget target) {
    const auto compilation = tests::require(compiler.compile(tests::wgsl_request(stage, target)));
    const auto package = tests::require(rhi::ShaderPackage::load(compilation.package.bytes(),
        {.allowExperimentalMetalGeneration = true}));
    return tests::require(device.create_shader(package, target, "main", stage));
}

void graphics(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto vertex = load(device, compiler, rhi::ShaderStage::vertex, target);
    auto fragment = load(device, compiler, rhi::ShaderStage::fragment, target);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {
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
    auto commands = tests::require(pool.allocate());
    tests::require(commands.begin());
    auto render = tests::require(commands.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &output, .clear = {0, 0, 0, 1}}}}));
    tests::require(render.bind_pipeline(pipeline));
    tests::require(render.bind_vertex_buffer(0, vertices));
    tests::require(render.bind_group(1, group));
    tests::require(render.draw(3));
    tests::require(render.end());
    tests::require(commands.end());
    tests::submit_graphics(device, commands);
    tests::check_color(device, output, {std::byte{64}, std::byte{64}, std::byte{32}, std::byte{255}},
                       "WGSL generated resource-backed graphics");
}

void compute(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto shader = load(device, compiler, rhi::ShaderStage::compute, target);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {{.binding = 9, .type = rhi::BindingType::storage_buffer,
                               .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 16}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto pipeline = tests::require(device.create_compute_pipeline({.computeShader = &shader, .layout = &layout}));
    auto buffer = tests::require(device.create_buffer({
        .size = 16, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
    const std::array<uint32_t, 4> initial{};
    tests::require(buffer.write(0, std::as_bytes(std::span{initial})));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena, .entries = {{.binding = 9, .buffer = &buffer, .size = 16}}}));
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
    std::array<uint32_t, 4> actual{};
    tests::require(buffer.read(0, std::as_writable_bytes(std::span{actual})));
    if (actual != std::array<uint32_t, 4>{12, 13, 14, 15})
        throw std::runtime_error("WGSL native compute exact readback mismatch");
    std::cout << "WGSL compute exact readback 12,13,14,15\n";
}
} // namespace

int main() {
    try {
        shader::Compiler compiler;
        for (const auto target : {rhi::ShaderTarget::spirv, rhi::ShaderTarget::metal_source}) {
            auto instance = tests::require(target == rhi::ShaderTarget::spirv ?
                rhi::create_vulkan_instance({.enableValidation = true}) :
                rhi::create_metal_instance({.enableValidation = true}));
            if (target == rhi::ShaderTarget::spirv &&
                rhi::detail::last_vulkan_validation_state_for_testing() != rhi::detail::VulkanValidationState::active)
                throw std::runtime_error("WGSL proof requires active Vulkan validation");
            auto adapter = tests::require(instance.adapter(0));
            std::cout << "WGSL native " << rhi::shader_target_name(target) << ": " << adapter.info().name << '\n';
            auto device = tests::require(adapter.request_device());
            graphics(device, compiler, target);
            compute(device, compiler, target);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

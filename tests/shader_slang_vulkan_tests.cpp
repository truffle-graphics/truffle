#include "shader_test_utils.hpp"
#include "truffle/shader/compiler.hpp"
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"

#include <array>
#include <iostream>

namespace {
using namespace truffle;

void graphics(rhi::Device& device, shader::Compiler& compiler, shader::CompileRequest request) {
    request.defines = {{"QUALITY", "3"}};
    const auto compile = [&](rhi::ShaderStage stage, const char* entry) {
        request.stage = stage;
        request.entryPoint = entry;
        const auto compilation = tests::require(compiler.compile(request));
        auto package = tests::require(rhi::ShaderPackage::load(compilation.package.bytes()));
        return tests::require(device.create_shader(package, rhi::ShaderTarget::spirv, entry, stage,
            package.desc().variants[0].permutation));
    };
    auto vertex = compile(rhi::ShaderStage::vertex, "vertexMain");
    auto fragment = compile(rhi::ShaderStage::fragment, "fragmentMain");
    auto layout = tests::require(device.create_pipeline_layout({}));
    auto pipeline = tests::require(device.create_pipeline({
        .vertexShader = &vertex, .fragmentShader = &fragment, .layout = &layout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}}}));
    auto target = tests::require(device.create_texture({
        .extent = {16, 16, 1}, .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source}));
    constexpr size_t rowPitch = 256;
    auto readback = tests::require(device.create_buffer({
        .size = rowPitch * 16, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback}));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto queue = tests::require(device.queue(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    rhi::BarrierBatch before;
    before.textures.push_back({
        .texture = &target, .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::color_attachment,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::color_attachment_output,
        .sourceAccess = rhi::Access::none, .destinationAccess = rhi::Access::color_attachment_write});
    tests::require(list.barrier(before));
    auto render = tests::require(list.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &target, .clear = {0, 0, 0, 1}}}}));
    tests::require(render.bind_pipeline(pipeline));
    tests::require(render.draw(3));
    tests::require(render.end());
    rhi::BarrierBatch after;
    after.textures.push_back({
        .texture = &target, .oldLayout = rhi::TextureLayout::color_attachment,
        .newLayout = rhi::TextureLayout::transfer_source,
        .sourceStages = rhi::PipelineStage::color_attachment_output,
        .destinationStages = rhi::PipelineStage::copy,
        .sourceAccess = rhi::Access::color_attachment_write, .destinationAccess = rhi::Access::transfer_read});
    tests::require(list.barrier(after));
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_texture_to_buffer(target, readback,
        {.layout = {.bytesPerRow = rowPitch, .rowsPerImage = 16},
         .texture = {.extent = {16, 16, 1}}}));
    tests::require(copy.end());
    tests::require(list.end());
    const std::array<rhi::CommandList*, 1> lists{&list};
    tests::require(queue.submit(lists));
    std::array<std::byte, rowPitch * 16> actual{};
    tests::require(readback.read(0, actual));
    const std::array expected{std::byte{78}, std::byte{0}, std::byte{0}, std::byte{255}};
    for (size_t y = 0; y < 16; ++y)
        for (size_t x = 0; x < 16; ++x)
            for (size_t channel = 0; channel < 4; ++channel)
                TRUFFLE_SHADER_CHECK(actual[y * rowPitch + x * 4 + channel] == expected[channel]);
    std::cout << "Slang native vertex/fragment: exact 256 pixels RGBA(78,0,0,255)\n";
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
    auto device = tests::require(adapter.request_device());
    shader::Compiler compiler;
    shader::CompileRequest request;
    request.name = "slang-native";
    request.source = tests::require(shader::read_source(TRUFFLE_SLANG_FIXTURES "/main.slang"));
    request.language = rhi::ShaderSourceLanguage::slang;
    request.stage = rhi::ShaderStage::compute;
    request.entryPoint = "computeMain";
    request.includeRoots = {TRUFFLE_SLANG_FIXTURES};
    graphics(device, compiler, request);
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 4}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto arena = tests::require(device.create_descriptor_arena());
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::compute));
    auto queue = tests::require(device.queue(rhi::QueueKind::compute));
    for (uint32_t quality : {3u, 4u}) {
        request.defines = {{"QUALITY", std::to_string(quality)}};
        const auto compilation = tests::require(compiler.compile(request));
        TRUFFLE_SHADER_CHECK(compilation.dependencies.size() == 3);
        auto package = tests::require(rhi::ShaderPackage::load(compilation.package.bytes()));
        auto shader = tests::require(device.create_shader(package, rhi::ShaderTarget::spirv,
            "computeMain", rhi::ShaderStage::compute, package.desc().variants[0].permutation));
        auto pipeline = tests::require(device.create_compute_pipeline({
            .computeShader = &shader, .layout = &layout, .requiredWorkgroupSize = {4, 1, 1}}));
        auto output = tests::require(device.create_buffer({
            .size = 36, .usage = rhi::BufferUsage::storage, .memory = rhi::MemoryDomain::readback}));
        std::array<uint32_t, 9> actual;
        actual.fill(0xdeadbeefu);
        tests::require(output.write(0, std::as_bytes(std::span{actual})));
        auto group = tests::require(device.create_bind_group({
            .layout = &groupLayout, .arena = &arena,
            .entries = {{.binding = 0, .buffer = &output, .size = 36}}}));
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
        for (size_t index = 0; index < 8; ++index)
            TRUFFLE_SHADER_CHECK(actual[index] == quality * 100 + index + 7);
        TRUFFLE_SHADER_CHECK(actual[8] == 0xdeadbeefu);
    }
    std::cout << "Slang native " << adapter.info().name
              << ": imported compute QUALITY=3/4 exact 8-word readbacks and untouched tail; validation "
              << (validation == rhi::detail::VulkanValidationState::active ? "active" : "not installed") << '\n';
    return 0;
}
}

int main() {
    try { return run(); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

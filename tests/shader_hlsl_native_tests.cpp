#include "shader_hlsl_fixtures.hpp"
#include "renderer_native_test_utils.hpp"
#if defined(TFL_HLSL_TEST_VULKAN)
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"
#endif
#if defined(TFL_HLSL_TEST_DIRECT3D)
#include "truffle/rhi/direct3d_backend.hpp"
#include "direct3d_backend_test.hpp"
#endif
#if defined(TFL_HLSL_TEST_METAL)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TFL_HLSL_TEST_OPENGL)
#include "truffle/rhi/opengl_backend.hpp"
#endif

#include <algorithm>
#include <iostream>

namespace {
using namespace truffle;

rhi::Shader load(rhi::Device& device, shader::Compiler& compiler, shader::CompileRequest request) {
    request.allowExperimentalMetalGeneration = true;
    const auto result = tests::require(compiler.compile(request));
    const auto package = tests::require(rhi::ShaderPackage::load(
        result.package.bytes(), {.allowExperimentalMetalGeneration = true}));
    return tests::require(device.create_shader(package, request.target, request.entryPoint, request.stage,
        package.desc().variants.front().permutation));
}

void copy_buffer(rhi::Device& device, rhi::Buffer& source, rhi::Buffer& destination, std::size_t size) {
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_buffer(source, 0, destination, 0, size));
    tests::require(copy.end());
    tests::require(list.end());
    tests::submit_graphics(device, list);
}

void graphics(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target) {
    auto vertex = load(device, compiler, tests::hlsl_request(rhi::ShaderStage::vertex, target));
    auto fragment = load(device, compiler, tests::hlsl_request(rhi::ShaderStage::fragment, target));
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
        .size = 256, .usage = rhi::BufferUsage::uniform, .memory = rhi::MemoryDomain::upload}));
    // Avoid halfway UNORM quantization, whose tie-breaking can vary by GPU.
    const std::array<float, 4> tint{64.0F / 255.0F, 128.0F / 255.0F, 1, 1};
    tests::require(uniform.write(0, std::as_bytes(std::span{tint})));
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena, .entries = {{.binding = 7, .buffer = &uniform, .size = 16}}}));
    auto output = tests::make_color_target(device);
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto render = tests::require(list.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &output, .clear = {0, 0, 0, 0}}}}));
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

void compute(rhi::Device& device, shader::Compiler& compiler, rhi::ShaderTarget target, bool raw = false) {
    const bool direct3d = target == rhi::ShaderTarget::dxil;
    auto request = tests::hlsl_request(rhi::ShaderStage::compute, target);
    if (raw) {
        request.source = {"raw-compute.hlsl", R"(
cbuffer Params : register(b7, space1) { uint4 bias; };
RWByteAddressBuffer output : register(u9, space1);
[numthreads(2, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) { output.Store(id.x * 4, bias.x + id.x + QUALITY); }
)"};
    }
    auto shader = load(device, compiler, std::move(request));
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {
            {.binding = 7, .type = rhi::BindingType::uniform_buffer, .visibility = rhi::ShaderStageMask::compute,
             .minimumBufferSize = 16},
            {.binding = 9, .type = rhi::BindingType::storage_buffer, .visibility = rhi::ShaderStageMask::compute,
             .minimumBufferSize = 4}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto pipeline = tests::require(device.create_compute_pipeline({.computeShader = &shader, .layout = &layout}));
    auto uniform = tests::require(device.create_buffer({
        .size = 256, .usage = rhi::BufferUsage::uniform, .memory = rhi::MemoryDomain::upload}));
    const std::array bias{100u, 0u, 0u, 0u};
    tests::require(uniform.write(0, std::as_bytes(std::span{bias})));
    auto buffer = tests::require(device.create_buffer({
        .size = 32,
        .usage = direct3d ? rhi::BufferUsage::storage | rhi::BufferUsage::copy_source |
                               rhi::BufferUsage::copy_destination : rhi::BufferUsage::storage,
        .memory = direct3d ? rhi::MemoryDomain::device_local : rhi::MemoryDomain::readback}));
    std::array<std::uint32_t, 8> actual;
    actual.fill(0xdeadbeefu);
    if (direct3d) {
        auto upload = tests::require(device.create_buffer({
            .size = sizeof(actual), .usage = rhi::BufferUsage::copy_source,
            .memory = rhi::MemoryDomain::upload}));
        tests::require(upload.write(0, std::as_bytes(std::span{actual})));
        copy_buffer(device, upload, buffer, sizeof(actual));
    } else {
        tests::require(buffer.write(0, std::as_bytes(std::span{actual})));
    }
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena,
        .entries = {{.binding = 7, .buffer = &uniform, .size = 16}, {.binding = 9, .buffer = &buffer, .size = 32}}}));
    const auto queueKind = direct3d ? rhi::QueueKind::graphics : rhi::QueueKind::compute;
    auto pool = tests::require(device.create_command_pool(queueKind));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto encoder = tests::require(list.begin_compute());
    tests::require(encoder.bind_pipeline(pipeline));
    tests::require(encoder.bind_group(1, group));
    tests::require(encoder.dispatch(2, 1, 1));
    tests::require(encoder.end());
    tests::require(list.end());
    auto queue = tests::require(device.queue(queueKind));
    const std::array<rhi::CommandList*, 1> lists{&list};
    tests::require(queue.submit(lists));
    if (direct3d) {
        auto readback = tests::require(device.create_buffer({
            .size = sizeof(actual), .usage = rhi::BufferUsage::copy_destination,
            .memory = rhi::MemoryDomain::readback}));
        copy_buffer(device, buffer, readback, sizeof(actual));
        tests::require(readback.read(0, std::as_writable_bytes(std::span{actual})));
    } else {
        tests::require(buffer.read(0, std::as_writable_bytes(std::span{actual})));
    }
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != (i < 4 ? 107u + i : 0xdeadbeefu))
            throw std::runtime_error("HLSL compute output/sentinel mismatch");
    std::cout << "HLSL " << rhi::shader_target_name(target)
              << (raw ? " raw" : " structured")
              << ": exact compute 107..110, four untouched sentinel values\n";
}

#if defined(TFL_HLSL_TEST_DIRECT3D)
void structured_arrays(rhi::Device& device, shader::Compiler& compiler) {
    shader::CompileRequest request;
    request.source = {"structured-arrays.hlsl", R"(
StructuredBuffer<uint3> inputs[2] : register(t2, space1);
RWStructuredBuffer<uint3> outputs[2] : register(u6, space1);
[numthreads(2, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    outputs[1][id.x] = inputs[1][id.x] + uint3(10, 20, 30);
})"};
    request.language = rhi::ShaderSourceLanguage::hlsl;
    request.target = rhi::ShaderTarget::dxil;
    request.stage = rhi::ShaderStage::compute;
    const auto result = tests::require(compiler.compile(request));
    const auto package = tests::require(rhi::ShaderPackage::load(result.package.bytes()));
    if (package.desc().remaps.size() != 4 ||
        !std::all_of(package.desc().remaps.begin(), package.desc().remaps.end(),
                     [](const auto& map) { return map.nativeBufferStride == 12; }))
        throw std::runtime_error("DXIL structured array stride metadata mismatch");
    auto shader = tests::require(device.create_shader(
        package, request.target, request.entryPoint, request.stage,
        package.desc().variants.front().permutation));
    auto groupLayout = tests::require(device.create_bind_group_layout({
        .group = 1, .entries = {
            {.binding = 2, .type = rhi::BindingType::storage_buffer, .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 12},
            {.binding = 6, .type = rhi::BindingType::storage_buffer, .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::compute, .minimumBufferSize = 12}}}));
    auto layout = tests::require(device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}}));
    auto pipeline = tests::require(device.create_compute_pipeline({.computeShader = &shader, .layout = &layout}));
    std::array<rhi::Buffer, 4> buffers;
    std::array<std::uint32_t, 12> words;
    constexpr auto byteCount = sizeof(words);
    auto upload = tests::require(device.create_buffer({
        .size = byteCount, .usage = rhi::BufferUsage::copy_source, .memory = rhi::MemoryDomain::upload}));
    for (std::size_t index = 0; index < buffers.size(); ++index) {
        buffers[index] = tests::require(device.create_buffer({
            .size = byteCount, .usage = rhi::BufferUsage::storage |
                rhi::BufferUsage::copy_source | rhi::BufferUsage::copy_destination}));
        words.fill(0xdeadbeefu);
        if (index < 2)
            for (std::size_t i = 0; i < words.size(); ++i)
                words[i] = static_cast<std::uint32_t>(i + (index == 0 ? 100 : 1));
        tests::require(upload.write(0, std::as_bytes(std::span{words})));
        copy_buffer(device, upload, buffers[index], byteCount);
    }
    auto arena = tests::require(device.create_descriptor_arena());
    auto group = tests::require(device.create_bind_group({
        .layout = &groupLayout, .arena = &arena, .entries = {
            {.binding = 2, .arrayElement = 0, .buffer = &buffers[0], .size = byteCount},
            {.binding = 2, .arrayElement = 1, .buffer = &buffers[1], .size = byteCount},
            {.binding = 6, .arrayElement = 0, .buffer = &buffers[2], .size = byteCount},
            {.binding = 6, .arrayElement = 1, .buffer = &buffers[3], .size = byteCount}}}));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto compute = tests::require(list.begin_compute());
    tests::require(compute.bind_pipeline(pipeline));
    tests::require(compute.bind_group(1, group));
    tests::require(compute.dispatch(1, 1, 1));
    tests::require(compute.end());
    tests::require(list.end());
    tests::submit_graphics(device, list);
    auto readback = tests::require(device.create_buffer({
        .size = byteCount, .usage = rhi::BufferUsage::copy_destination, .memory = rhi::MemoryDomain::readback}));
    for (std::size_t index = 2; index < buffers.size(); ++index) {
        copy_buffer(device, buffers[index], readback, byteCount);
        tests::require(readback.read(0, std::as_writable_bytes(std::span{words})));
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto expected = index == 3 && i < 6
                ? static_cast<std::uint32_t>(i + 1 + (i % 3 + 1) * 10) : 0xdeadbeefu;
            if (words[i] != expected)
                throw std::runtime_error("DXIL structured array output/sentinel mismatch");
        }
    }
    std::cout << "HLSL dxil: exact structured SRV/UAV arrays, stride 12, untouched sentinels\n";
}
#endif
} // namespace

int main() {
    try {
        shader::Compiler compiler;
        std::vector<rhi::ShaderTarget> targets;
#if defined(TFL_HLSL_TEST_VULKAN)
        targets.push_back(rhi::ShaderTarget::spirv);
#endif
#if defined(TFL_HLSL_TEST_DIRECT3D)
        targets.push_back(rhi::ShaderTarget::dxil);
#endif
#if defined(TFL_HLSL_TEST_METAL)
        targets.push_back(rhi::ShaderTarget::metal_source);
#endif
#if defined(TFL_HLSL_TEST_OPENGL)
        targets.push_back(rhi::ShaderTarget::glsl);
#endif
        for (const auto target : targets) {
            const auto create = [&]() -> rhi::Result<rhi::Instance> {
#if defined(TFL_HLSL_TEST_DIRECT3D)
                if (target == rhi::ShaderTarget::dxil)
                    return rhi::create_direct3d12_instance({.enableValidation = true});
#endif
#if defined(TFL_HLSL_TEST_METAL)
                if (target == rhi::ShaderTarget::metal_source)
                    return rhi::create_metal_instance({.enableValidation = true});
#endif
#if defined(TFL_HLSL_TEST_OPENGL)
                if (target == rhi::ShaderTarget::glsl)
                    return rhi::create_opengl_instance({.enableValidation = true});
#endif
#if defined(TFL_HLSL_TEST_VULKAN)
                if (target == rhi::ShaderTarget::spirv)
                    return rhi::create_vulkan_instance({.enableValidation = true});
#endif
                return rhi::Status::failure(rhi::StatusCode::unsupported, "No native HLSL test backend");
            };
            auto instance = tests::require(create());
#if defined(TFL_HLSL_TEST_VULKAN)
            if (target == rhi::ShaderTarget::spirv &&
                rhi::detail::last_vulkan_validation_state_for_testing() != rhi::detail::VulkanValidationState::active)
                throw std::runtime_error("HLSL native evidence requires active Vulkan validation");
#endif
            auto adapter = tests::require(instance.adapter(0));
            auto device = tests::require(adapter.request_device());
            std::cout << "HLSL native adapter: " << adapter.info().name << '\n';
            graphics(device, compiler, target);
            if (target != rhi::ShaderTarget::glsl) compute(device, compiler, target);
#if defined(TFL_HLSL_TEST_DIRECT3D)
            if (target == rhi::ShaderTarget::dxil) {
                compute(device, compiler, target, true);
                structured_arrays(device, compiler);
                tests::require(rhi::detail::direct3d_validation_status_for_testing());
            }
#endif
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

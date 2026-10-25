#pragma once

#include "gl_graphics_tests.hpp"

namespace truffle::tests {

inline void verify_gl_bindings(rhi::Result<rhi::Instance> result, bool desktop,
                                const std::size_t* diagnostics) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapter = instance.adapter(0);
    assert(adapter.ok());
    const auto info = adapter.value().info();
    assert(info.bindings.descriptorArrays && info.bindings.dynamicOffsets &&
           info.bindings.immutableSamplers && info.bindings.pushConstants);
    auto requested = adapter.value().request_device();
    assert(requested.ok());
    auto device = std::move(requested).value();
    const auto target = desktop ? rhi::ShaderTarget::glsl : rhi::ShaderTarget::glsl_es;
    auto vertex = gl_fixture_shader(device, desktop, rhi::ShaderStage::vertex, R"(
const vec2 positions[3] = vec2[3](vec2(-0.75,-0.75),vec2(0.75,-0.75),vec2(0.0,0.75));
void main() { gl_Position = vec4(positions[gl_VertexID],0.0,1.0); }
)");
    const std::string fragmentSource = R"(
layout(std140) uniform Params { vec4 color; };
layout(std140) uniform TruffleFragmentPushConstants { vec4 tint; };
uniform sampler2D images[2];
uniform float gain;
layout(location=0) out vec4 outputColor;
void main() {
    outputColor = mix(texture(images[0],vec2(0.5)),texture(images[1],vec2(0.5)),tint.x)
                  * color * gain;
}
)";
    rhi::ShaderReflection reflection{
        .bindings = {
            {.name = "Params", .stage = rhi::ShaderStage::fragment,
             .type = rhi::ResourceBindingType::buffer, .group = 2, .binding = 1,
             .minimumSize = 16},
            {.name = "images", .stage = rhi::ShaderStage::fragment,
             .type = rhi::ResourceBindingType::texture, .group = 2, .binding = 5,
             .arrayCount = 2},
            {.name = "images[0]", .stage = rhi::ShaderStage::fragment,
             .type = rhi::ResourceBindingType::sampler, .group = 2, .binding = 8},
            {.name = "images[1]", .stage = rhi::ShaderStage::fragment,
             .type = rhi::ResourceBindingType::sampler, .group = 2, .binding = 9}},
        .pushConstants = {{.stage = rhi::ShaderStage::fragment, .size = 16}},
        .specializationConstants = {{.id = 7, .name = "gain",
                                     .type = rhi::ShaderValueType::float32,
                                     .defaultValueBits = 0x3f800000u}}};
    std::vector<rhi::ShaderBindingRemap> remaps{
        {.target = target, .stage = rhi::ShaderStage::fragment,
         .group = 2, .binding = 1, .nativeBinding = 6},
        {.target = target, .stage = rhi::ShaderStage::fragment,
         .group = 2, .binding = 5, .nativeBinding = 3},
        {.target = target, .stage = rhi::ShaderStage::fragment,
         .group = 2, .binding = 5, .arrayElement = 1, .nativeBinding = 4},
        {.target = target, .stage = rhi::ShaderStage::fragment,
         .group = 2, .binding = 8, .nativeBinding = 3},
        {.target = target, .stage = rhi::ShaderStage::fragment,
         .group = 2, .binding = 9, .nativeBinding = 4}};
    auto fragment = gl_fixture_shader(device, desktop, rhi::ShaderStage::fragment,
                                       fragmentSource, reflection, remaps);
    auto samplerResult = device.create_sampler();
    assert(samplerResult.ok());
    auto sampler = std::move(samplerResult).value();
    auto groupLayoutResult = device.create_bind_group_layout({
        .group = 2,
        .entries = {
            {.binding = 1, .type = rhi::BindingType::uniform_buffer,
             .visibility = rhi::ShaderStageMask::fragment, .dynamicOffset = true,
             .minimumBufferSize = 16},
            {.binding = 5, .type = rhi::BindingType::sampled_texture, .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 8, .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment, .immutableSampler = &sampler},
            {.binding = 9, .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment}}});
    assert(groupLayoutResult.ok());
    auto groupLayout = std::move(groupLayoutResult).value();
    auto layoutResult = device.create_pipeline_layout({
        .bindGroupLayouts = {&groupLayout},
        .pushConstants = {{.stage = rhi::ShaderStage::fragment, .size = 16}}});
    assert(layoutResult.ok());
    auto layout = std::move(layoutResult).value();
    const auto alignment = info.bindings.minUniformBufferOffsetAlignment;
    const auto offset = std::max<std::uint32_t>(alignment, 16);
    auto bufferResult = device.create_buffer({
        .size = offset + 16, .usage = rhi::BufferUsage::uniform,
        .memory = rhi::MemoryDomain::upload});
    assert(bufferResult.ok());
    auto buffer = std::move(bufferResult).value();
    const std::array<float, 4> white{1, 1, 1, 1};
    assert(buffer.write(offset, std::as_bytes(std::span{white})).ok());
    const std::array red{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    const std::array green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    const auto sampled = [&](const std::array<std::byte, 4>& color) {
        auto texture = device.create_texture({
            .usage = rhi::TextureUsage::sampled, .memory = rhi::MemoryDomain::upload});
        assert(texture.ok());
        assert(texture.value().write({.extent = {1, 1, 1}}, color).ok());
        return std::move(texture).value();
    };
    auto redTexture = sampled(red);
    auto greenTexture = sampled(green);
    auto redViewResult = device.create_texture_view(redTexture);
    auto greenViewResult = device.create_texture_view(greenTexture);
    assert(redViewResult.ok() && greenViewResult.ok());
    auto redView = std::move(redViewResult).value();
    auto greenView = std::move(greenViewResult).value();
    auto arenaResult = device.create_descriptor_arena();
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    rhi::BindGroupDesc groupDesc{
        .layout = &groupLayout, .arena = &arena,
        .entries = {{.binding = 1, .buffer = &buffer, .size = 16},
                    {.binding = 5, .textureView = &redView},
                    {.binding = 5, .arrayElement = 1, .textureView = &greenView},
                    {.binding = 9, .sampler = &sampler}}};
    auto groupResult = device.create_bind_group(groupDesc);
    assert(groupResult.ok());
    auto group = std::move(groupResult).value();
    rhi::PipelineDesc pipelineDesc{
        .vertexShader = &vertex, .fragmentShader = &fragment, .layout = &layout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}}};
    auto pipelineResult = device.create_pipeline(pipelineDesc);
    assert(pipelineResult.ok());
    auto pipeline = std::move(pipelineResult).value();
    constexpr std::uint32_t size = 16;
    auto targetResult = device.create_texture({
        .extent = {size, size, 1},
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source});
    auto readResult = device.create_buffer({
        .size = size * size * 4, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback});
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(targetResult.ok() && readResult.ok() && poolResult.ok() && queueResult.ok());
    auto renderTarget = std::move(targetResult).value();
    auto readback = std::move(readResult).value();
    auto pool = std::move(poolResult).value();
    auto queue = std::move(queueResult).value();
    const auto render = [&](rhi::Pipeline& selected, bool reset) {
        auto allocated = pool.allocate();
        assert(allocated.ok());
        auto list = std::move(allocated).value();
        assert(list.begin().ok());
        auto begun = list.begin_rendering({
            .extent = {size, size}, .colorAttachments = {{.texture = &renderTarget}}});
        assert(begun.ok());
        auto encoder = std::move(begun).value();
        assert(encoder.bind_pipeline(selected).ok());
        const std::array offsets{offset};
        const std::array invalidOffsets{offset + 16};
        assert(!encoder.bind_group(2, group, invalidOffsets).ok());
        assert(encoder.bind_group(2, group, offsets).ok());
        const std::array<float, 4> tint{1, 0, 0, 0};
        assert(encoder.push_constants(rhi::ShaderStageMask::fragment, 0,
                                       std::as_bytes(std::span{tint})).ok());
        if (reset) {
            assert(arena.reset().ok());
            assert(!encoder.bind_group(2, group, offsets).ok());
        }
        assert(encoder.draw(3).ok() && encoder.end().ok());
        auto copied = list.begin_copy();
        assert(copied.ok());
        auto copy = std::move(copied).value();
        assert(copy.copy_texture_to_buffer(
            renderTarget, readback, {.texture = {.extent = {size, size, 1}}}).ok());
        assert(copy.end().ok() && list.end().ok());
        const std::array<rhi::CommandList*, 1> lists{&list};
        assert(queue.submit(lists).ok());
        std::array<std::byte, 4> center{};
        assert(readback.read((8 * size + 8) * 4, center).ok());
        return center;
    };
    assert(render(pipeline, true) == green);
    groupResult = device.create_bind_group(groupDesc);
    assert(groupResult.ok());
    group = std::move(groupResult).value();
    pipelineDesc.specializationConstants = {
        {.id = 7, .type = rhi::ShaderValueType::float32, .valueBits = 0}};
    auto zeroResult = device.create_pipeline(pipelineDesc);
    assert(zeroResult.ok());
    auto zero = std::move(zeroResult).value();
    const std::array<std::byte, 4> transparent{};
    assert(render(zero, false) == transparent);
    assert(render(pipeline, false) == green);
    reflection.bindings.front().name = "MissingBlock";
    auto mismatch = gl_fixture_shader(device, desktop, rhi::ShaderStage::fragment,
                                       fragmentSource, reflection, remaps);
    pipelineDesc.fragmentShader = &mismatch;
    assert(device.create_pipeline(pipelineDesc).status().code ==
           rhi::StatusCode::invalid_argument);
    if (diagnostics) assert(*diagnostics == 0);
}

inline void verify_gl_compute(rhi::Result<rhi::Instance> result, bool desktop,
                               const std::size_t* diagnostics) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapter = instance.adapter(0);
    assert(adapter.ok());
    auto requested = adapter.value().request_device();
    assert(requested.ok());
    auto device = std::move(requested).value();
    if (!desktop) {
        const auto shader = device.create_shader({
            .stage = rhi::ShaderStage::compute,
            .code = native_shader_bytes("#version 300 es\nvoid main() {}")});
        assert(shader.status().code == rhi::StatusCode::unsupported);
        return;
    }
    rhi::ShaderReflection computeReflection{
        .bindings = {{.name = "Data", .stage = rhi::ShaderStage::compute,
                      .type = rhi::ResourceBindingType::buffer, .group = 1,
                      .binding = 3, .minimumSize = 16, .readOnly = false}},
        .requiredWorkgroupSize = {4, 1, 1}, .preferredWorkgroupSize = {4, 1, 1}};
    auto computeShader = gl_fixture_shader(device, true, rhi::ShaderStage::compute, R"(
layout(local_size_x=4) in;
layout(std430) buffer Data { uvec4 values; };
void main() { values[gl_LocalInvocationIndex] = 10u + gl_LocalInvocationIndex; }
)", computeReflection);
    auto vertex = gl_fixture_shader(device, true, rhi::ShaderStage::vertex, R"(
const vec2 positions[3]=vec2[3](vec2(-0.75,-0.75),vec2(0.75,-0.75),vec2(0.0,0.75));
void main() { gl_Position=vec4(positions[gl_VertexID],0.0,1.0); }
)");
    auto fragmentReflection = computeReflection;
    fragmentReflection.bindings.front().stage = rhi::ShaderStage::fragment;
    fragmentReflection.bindings.front().readOnly = true;
    auto fragment = gl_fixture_shader(device, true, rhi::ShaderStage::fragment, R"(
layout(std430) readonly buffer Data { uvec4 values; };
layout(location=0) out vec4 outputColor;
void main() { outputColor=values.z==12u ? vec4(0,0,1,1) : vec4(1,0,0,1); }
)", fragmentReflection);
    auto groupLayoutResult = device.create_bind_group_layout({
        .group = 1, .entries = {{.binding = 3, .type = rhi::BindingType::storage_buffer,
                                 .minimumBufferSize = 16}}});
    assert(groupLayoutResult.ok());
    auto groupLayout = std::move(groupLayoutResult).value();
    auto layoutResult = device.create_pipeline_layout({.bindGroupLayouts = {&groupLayout}});
    assert(layoutResult.ok());
    auto layout = std::move(layoutResult).value();
    auto computeResult = device.create_compute_pipeline({
        .computeShader = &computeShader, .layout = &layout});
    auto pipelineResult = device.create_pipeline({
        .vertexShader = &vertex, .fragmentShader = &fragment, .layout = &layout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}}});
    assert(computeResult.ok() && pipelineResult.ok());
    auto computePipeline = std::move(computeResult).value();
    auto pipeline = std::move(pipelineResult).value();
    auto storageResult = device.create_buffer({
        .size = 16, .usage = rhi::BufferUsage::storage | rhi::BufferUsage::copy_source});
    auto readResult = device.create_buffer({
        .size = 16 * 16 * 4 + 16, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback});
    auto indirectResult = device.create_buffer({
        .size = 12, .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload});
    assert(storageResult.ok() && readResult.ok() && indirectResult.ok());
    auto storage = std::move(storageResult).value();
    auto readback = std::move(readResult).value();
    auto indirect = std::move(indirectResult).value();
    const std::array<std::uint32_t, 3> groups{1, 1, 1};
    assert(indirect.write(0, std::as_bytes(std::span{groups})).ok());
    auto arenaResult = device.create_descriptor_arena();
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    auto groupResult = device.create_bind_group({
        .layout = &groupLayout, .arena = &arena,
        .entries = {{.binding = 3, .buffer = &storage, .size = 16}}});
    auto targetResult = device.create_texture({
        .extent = {16, 16, 1},
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source});
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(groupResult.ok() && targetResult.ok() && poolResult.ok() && queueResult.ok());
    auto group = std::move(groupResult).value();
    auto target = std::move(targetResult).value();
    auto pool = std::move(poolResult).value();
    auto queue = std::move(queueResult).value();
    for (const bool useIndirect : {false, true}) {
        auto allocated = pool.allocate();
        assert(allocated.ok());
        auto list = std::move(allocated).value();
        assert(list.begin().ok());
        auto begun = list.begin_compute();
        assert(begun.ok());
        auto compute = std::move(begun).value();
        assert(compute.bind_pipeline(computePipeline).ok());
        assert(compute.bind_group(1, group).ok());
        assert((useIndirect ? compute.dispatch_indirect(indirect, 0)
                             : compute.dispatch(1, 1, 1)).ok());
        assert(compute.end().ok());
        assert(list.barrier({.buffers = {{
            .buffer = &storage, .sourceStages = rhi::PipelineStage::compute_shader,
            .destinationStages = rhi::PipelineStage::fragment_shader,
            .sourceAccess = rhi::Access::shader_write,
            .destinationAccess = rhi::Access::shader_read}}}).ok());
        auto rendered = list.begin_rendering({
            .extent = {16, 16}, .colorAttachments = {{.texture = &target}}});
        assert(rendered.ok());
        auto render = std::move(rendered).value();
        assert(render.bind_pipeline(pipeline).ok() && render.bind_group(1, group).ok());
        assert(render.draw(3).ok() && render.end().ok());
        auto copied = list.begin_copy();
        assert(copied.ok());
        auto copy = std::move(copied).value();
        assert(copy.copy_buffer(storage, readback, {.size = 16}).ok());
        assert(copy.copy_texture_to_buffer(
            target, readback, {.bufferOffset = 16,
                               .texture = {.extent = {16, 16, 1}}}).ok());
        assert(copy.end().ok() && list.end().ok());
        const std::array<rhi::CommandList*, 1> lists{&list};
        assert(queue.submit(lists).ok());
        std::array<std::uint32_t, 4> words{};
        assert(readback.read(0, std::as_writable_bytes(std::span{words})).ok());
        assert((words == std::array<std::uint32_t, 4>{10, 11, 12, 13}));
        std::array<std::byte, 4> center{};
        assert(readback.read(16 + (8 * 16 + 8) * 4, center).ok());
        assert((center == std::array{std::byte{0}, std::byte{0},
                                     std::byte{255}, std::byte{255}}));
    }
    if (diagnostics) assert(*diagnostics == 0);
}

} // namespace truffle::tests

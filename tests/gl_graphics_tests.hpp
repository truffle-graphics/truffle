#pragma once

#include "native_backend_smoke.hpp"

namespace truffle::tests {

inline rhi::Shader gl_fixture_shader(rhi::Device& device, bool desktop,
                                     rhi::ShaderStage stage, std::string source,
                                     rhi::ShaderReflection reflection = {},
                                     std::vector<rhi::ShaderBindingRemap> remaps = {}) {
    rhi::ShaderPackageDesc desc;
    desc.name = "GL-family expanded native fixture";
    desc.sources = {{.path = "fixture.glsl",
                     .language = desktop ? rhi::ShaderSourceLanguage::glsl
                                         : rhi::ShaderSourceLanguage::glsl_es,
                     .sha256 = std::string(64, '0')}};
    desc.compilers = {{.name = "repository native-source fixture", .version = "1"}};
    desc.remaps = std::move(remaps);
    desc.variants = {{.target = desktop ? rhi::ShaderTarget::glsl
                                        : rhi::ShaderTarget::glsl_es,
                      .format = rhi::ShaderByteFormat::native_source,
                      .kind = rhi::ShaderVariantKind::native_override,
                      .stage = stage,
                      .entryPoint = "main",
                      .reflection = std::move(reflection),
                      .code = native_shader_bytes(
                          (desktop ? "#version 450 core\n" :
                                     "#version 300 es\nprecision highp float;\n") +
                          source)}};
    auto package = rhi::ShaderPackage::create(std::move(desc));
    assert(package.ok());
    auto shader = device.create_shader(
        package.value(), desktop ? rhi::ShaderTarget::glsl : rhi::ShaderTarget::glsl_es,
        "main", stage);
    assert(shader.ok());
    return std::move(shader).value();
}

inline void verify_expanded_gl_graphics(rhi::Result<rhi::Instance> result,
                                        bool desktop,
                                        const std::size_t* diagnostics) {
    assert(result.ok());
    auto instance = std::move(result).value();
    auto adapter = instance.adapter(0);
    assert(adapter.ok());
    auto requested = adapter.value().request_device();
    assert(requested.ok());
    auto device = std::move(requested).value();
    auto vertex = gl_fixture_shader(device, desktop, rhi::ShaderStage::vertex, R"(
layout(location=0) in vec2 position;
layout(location=1) in vec4 instanceColor;
out vec4 color;
void main() { gl_Position = vec4(position, 0.0, 1.0); color = instanceColor; }
)");
    auto fragment = gl_fixture_shader(device, desktop, rhi::ShaderStage::fragment, R"(
in vec4 color;
layout(location=0) out vec4 outputColor;
layout(location=1) out vec4 secondColor;
void main() { outputColor = color; secondColor = vec4(0.0, 0.0, 1.0, 1.0); }
)");
    const std::array<float, 6> positions{-0.75F, -0.75F, 0.75F, -0.75F, 0.0F, 0.75F};
    const std::array<float, 8> colors{1, 0, 0, 1, 0, 1, 0, 1};
    const std::array<std::uint16_t, 4> indices{0xffffu, 0, 1, 2};
    const auto upload = [&](std::span<const std::byte> bytes, rhi::BufferUsage usage) {
        auto buffer = device.create_buffer({
            .size = bytes.size(), .usage = usage, .memory = rhi::MemoryDomain::upload});
        assert(buffer.ok() && buffer.value().write(0, bytes).ok());
        return std::move(buffer).value();
    };
    auto vertices = upload(std::as_bytes(std::span{positions}), rhi::BufferUsage::vertex);
    auto instances = upload(std::as_bytes(std::span{colors}), rhi::BufferUsage::vertex);
    auto indexBuffer = upload(std::as_bytes(std::span{indices}), rhi::BufferUsage::index);
    const std::array<std::uint32_t, 5> draw{3, 2, 1, 0, 0};
    auto indirect = upload(std::as_bytes(std::span{draw}), rhi::BufferUsage::indirect);
    constexpr std::uint32_t extent = 16;
    const auto texture = [&](std::uint32_t samples = 1) {
        auto target = device.create_texture({
            .extent = {extent, extent, 1},
            .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
            .sampleCount = samples});
        assert(target.ok());
        return std::move(target).value();
    };
    auto first = texture();
    auto second = texture();
    auto depthResult = device.create_texture({
        .extent = {extent, extent, 1},
        .format = rhi::TextureFormat::depth24_unorm_stencil8,
        .usage = rhi::TextureUsage::depth_stencil_attachment});
    assert(depthResult.ok());
    auto depth = std::move(depthResult).value();
    auto poolResult = device.create_command_pool(rhi::QueueKind::graphics);
    auto queueResult = device.queue(rhi::QueueKind::graphics);
    assert(poolResult.ok() && queueResult.ok());
    auto pool = std::move(poolResult).value();
    auto queue = std::move(queueResult).value();
    auto readbackResult = device.create_buffer({
        .size = extent * extent * 4, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback});
    assert(readbackResult.ok());
    auto readback = std::move(readbackResult).value();
    const auto pixel = [&](rhi::Texture& target, std::uint32_t x = 8) {
        auto listResult = pool.allocate();
        assert(listResult.ok());
        auto list = std::move(listResult).value();
        assert(list.begin().ok());
        auto copyResult = list.begin_copy();
        assert(copyResult.ok());
        auto copy = std::move(copyResult).value();
        assert(copy.copy_texture_to_buffer(
            target, readback, {.texture = {.extent = {extent, extent, 1}}}).ok());
        assert(copy.end().ok() && list.end().ok());
        const std::array<rhi::CommandList*, 1> lists{&list};
        assert(queue.submit(lists).ok());
        std::array<std::byte, 4> value{};
        assert(readback.read(((extent / 2) * extent + x) * 4, value).ok());
        return value;
    };
    const std::array red{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    const std::array green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    const std::array blue{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    const std::array black{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    rhi::PipelineDesc pipelineDesc{
        .vertexShader = &vertex, .fragmentShader = &fragment,
        .vertexBuffers = {
            {.stride = 8, .attributes = {{.location = 0, .format = rhi::VertexFormat::float32x2}}},
            {.stride = 16, .stepMode = rhi::VertexStepMode::instance,
             .attributes = {{.location = 1, .format = rhi::VertexFormat::float32x4}}}},
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm},
                         {.format = rhi::TextureFormat::rgba8_unorm}}};
    const auto execute = [&](const rhi::PipelineDesc& desc, rhi::RenderPassDesc pass,
                             int kind, bool dynamic = false) {
        auto created = device.create_pipeline(desc);
        assert(created.ok());
        auto pipeline = std::move(created).value();
        auto allocated = pool.allocate();
        assert(allocated.ok());
        auto list = std::move(allocated).value();
        assert(list.begin().ok());
        auto begun = list.begin_rendering(pass);
        assert(begun.ok());
        auto render = std::move(begun).value();
        assert(render.bind_pipeline(pipeline).ok());
        assert(render.bind_vertex_buffer(0, vertices).ok());
        assert(render.bind_vertex_buffer(1, instances).ok());
        assert(render.bind_index_buffer(indexBuffer, kind == 2 ? 0 : 2,
                                         rhi::IndexFormat::uint16).ok());
        if (dynamic) {
            const std::array viewports{rhi::Viewport{0, 0, 16, 16, 0, 1}};
            const std::array scissors{rhi::ScissorRect{0, 0, 8, 16}};
            assert(render.set_viewports(0, viewports).ok());
            assert(render.set_scissors(0, scissors).ok());
            assert(render.set_blend_constant({0, 0, 0, 0}).ok());
            assert(render.set_stencil_reference(3).ok());
            assert(render.set_depth_bias(0, 0, 0).ok());
        }
        if (kind == 2) {
            auto status = render.draw_indirect(indirect, 0, true);
            if (!desktop) assert(status.code == rhi::StatusCode::unsupported);
            else assert(status.ok());
        } else if (kind == 1) {
            assert(render.draw_indexed(3, 2).ok());
        } else {
            assert(render.draw(3).ok());
        }
        assert(render.end().ok() && list.end().ok());
        const std::array<rhi::CommandList*, 1> lists{&list};
        assert(queue.submit(lists).ok());
    };
    rhi::RenderPassDesc pass{
        .extent = {extent, extent},
        .colorAttachments = {{.texture = &first}, {.texture = &second}}};
    execute(pipelineDesc, pass, 1);
    assert(pixel(first) == green && pixel(second) == blue);
    execute(pipelineDesc, pass, 0);
    assert(pixel(first) == red);
    execute(pipelineDesc, pass, 2);
    if (desktop) assert(pixel(first) == green);

    pipelineDesc.depthStencil.format = rhi::TextureFormat::depth24_unorm_stencil8;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = rhi::CompareOp::less;
    pipelineDesc.depthStencil.front.compare = rhi::CompareOp::equal;
    pipelineDesc.depthStencil.back.compare = rhi::CompareOp::equal;
    pipelineDesc.stencilReference = 3;
    pass.depthStencilAttachment = {.texture = &depth, .clearStencil = 3};
    execute(pipelineDesc, pass, 0);
    assert(pixel(first) == red);
    pass.depthStencilAttachment.clearDepth = 0.25F;
    execute(pipelineDesc, pass, 0);
    assert(pixel(first) == black);
    pass.depthStencilAttachment.clearDepth = 1.0F;
    pass.depthStencilAttachment.clearStencil = 2;
    execute(pipelineDesc, pass, 0);
    assert(pixel(first) == black);

    pipelineDesc.depthStencil = {};
    pass.depthStencilAttachment = {};
    pipelineDesc.dynamicState = rhi::DynamicState::viewport | rhi::DynamicState::scissor |
        rhi::DynamicState::blend_constant | rhi::DynamicState::stencil_reference |
        rhi::DynamicState::depth_bias;
    execute(pipelineDesc, pass, 0, true);
    assert(pixel(first, 6) == red && pixel(first, 10) == black);
    pipelineDesc.dynamicState = rhi::DynamicState::none;
    execute(pipelineDesc, pass, 0);
    assert(pixel(first, 10) == red);

    if (desktop) {
        auto multisample = texture(4);
        pipelineDesc.colorTargets.resize(1);
        pipelineDesc.multisample.sampleCount = 4;
        pass.colorAttachments = {{.texture = &multisample, .resolveTexture = &first}};
        execute(pipelineDesc, pass, 0);
        assert(pixel(first) == red);
    }
    pipelineDesc.rasterization.depthBiasClamp = 1.0F;
    assert(device.create_pipeline(pipelineDesc).status().code ==
           rhi::StatusCode::unsupported);
    if (diagnostics) assert(*diagnostics == 0);
}

} // namespace truffle::tests

#include "renderer_layer_composite.hpp"
#include "renderer_native_test_utils.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/source_program.hpp"
#include "truffle/renderer/texture.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <chrono>
#include <cstdlib>

namespace {

using namespace truffle;

// GUI-style 2D program: streamed quads, sampled atlas, vertex color, alpha blend.
constexpr const char* kShader = R"(#include <metal_stdlib>
using namespace metal;

struct QuadInput {
    float2 coordinate [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 tint [[attribute(2)]];
};

struct QuadOutput {
    float4 position [[position]];
    float2 uv [[user(locn0)]];
    float4 tint [[user(locn1)]];
};

vertex QuadOutput vertex_main(QuadInput input [[stage_in]]) {
    QuadOutput result;
    result.position = float4(input.coordinate, 0.0, 1.0);
    result.uv = input.uv;
    result.tint = input.tint;
    return result;
}

fragment float4 fragment_main(QuadOutput input [[stage_in]],
                              texture2d<float> atlas [[texture(0)]],
                              sampler atlasSampler [[sampler(0)]]) {
    return atlas.sample(atlasSampler, input.uv) * input.tint;
}
)";

struct QuadVertex {
    float coordinate[2];
    float uv[2];
    float tint[4];
};
static_assert(sizeof(QuadVertex) == 32);

shader::CompileRequest request(rhi::ShaderStage stage) {
    shader::CompileRequest result;
    result.name = "Renderer GUI primitives proof";
    result.source = {"gui_primitives.metal", kShader};
    result.language = rhi::ShaderSourceLanguage::msl;
    result.target = rhi::ShaderTarget::metal_source;
    result.stage = stage;
    result.entryPoint = stage == rhi::ShaderStage::vertex ? "vertex_main" : "fragment_main";
    rhi::ShaderReflection reflection;
    const auto add = [&](rhi::ShaderInterfaceDirection direction, std::uint32_t location,
                         const char* name, const char* type) {
        reflection.interfaces.push_back({stage, direction, location, name, type, "smooth"});
    };
    if (stage == rhi::ShaderStage::vertex) {
        add(rhi::ShaderInterfaceDirection::input, 0, "coordinate", "vec2");
        add(rhi::ShaderInterfaceDirection::input, 1, "uv", "vec2");
        add(rhi::ShaderInterfaceDirection::input, 2, "tint", "vec4");
        add(rhi::ShaderInterfaceDirection::output, 0, "uv", "vec2");
        add(rhi::ShaderInterfaceDirection::output, 1, "tint", "vec4");
    } else {
        add(rhi::ShaderInterfaceDirection::input, 0, "uv", "vec2");
        add(rhi::ShaderInterfaceDirection::input, 1, "tint", "vec4");
        add(rhi::ShaderInterfaceDirection::output, 0, "color", "vec4");
        reflection.bindings = {
            {.name = "atlas", .stage = stage, .type = rhi::ResourceBindingType::texture,
             .group = 0, .binding = 0},
            {.name = "atlasSampler", .stage = stage,
             .type = rhi::ResourceBindingType::sampler, .group = 0, .binding = 1},
        };
    }
    result.interfaceDeclaration = std::move(reflection);
    return result;
}

// Render-to-texture proof (#215): a writer fills a layer, a compositor samples it.
constexpr const char* kLayerShader = R"(#include <metal_stdlib>
using namespace metal;

struct FullscreenOutput {
    float4 position [[position]];
};

vertex FullscreenOutput fullscreen_vertex(uint id [[vertex_id]]) {
    const float2 corner = float2((id << 1) & 2, id & 2);
    FullscreenOutput result;
    result.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return result;
}

fragment float4 writer_fragment() {
    return float4(1.0, 0.0, 0.0, 1.0);
}

fragment float4 composite_fragment(float4 position [[position]],
                                   texture2d<float> layer [[texture(0)]],
                                   sampler layerSampler [[sampler(0)]]) {
    return layer.sample(layerSampler, position.xy / 16.0);
}
)";

shader::CompileRequest layer_request(rhi::ShaderStage stage, const char* entry,
                                     bool sampled = false) {
    shader::CompileRequest result;
    result.name = "Renderer render-to-texture proof";
    result.source = {"layer.metal", kLayerShader};
    result.language = rhi::ShaderSourceLanguage::msl;
    result.target = rhi::ShaderTarget::metal_source;
    result.stage = stage;
    result.entryPoint = entry;
    rhi::ShaderReflection reflection;
    if (stage == rhi::ShaderStage::fragment) {
        reflection.interfaces.push_back({stage, rhi::ShaderInterfaceDirection::output, 0,
                                         "color", "vec4", "smooth"});
    }
    if (sampled) {
        reflection.bindings = {
            {.name = "layer", .stage = stage, .type = rhi::ResourceBindingType::texture,
             .group = 0, .binding = 0},
            {.name = "layerSampler", .stage = stage,
             .type = rhi::ResourceBindingType::sampler, .group = 0, .binding = 1},
        };
    }
    result.interfaceDeclaration = std::move(reflection);
    return result;
}

void render_to_texture(rhi::Device& device) {
    auto bindings = tests::make_layer_bindings(device);
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    shader::Compiler compiler;
    rhi::PipelineDesc writerState;
    writerState.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    writerState.dynamicState = rhi::DynamicState::scissor;
    renderer::SourceProgram writer(programs, 1, writerState);
    tests::require(writer.rebuild(compiler, {
        .vertex = layer_request(rhi::ShaderStage::vertex, "fullscreen_vertex"),
        .fragment = layer_request(rhi::ShaderStage::fragment, "writer_fragment"),
    }));
    rhi::PipelineDesc compositorState;
    compositorState.layout = &bindings.pipelineLayout;
    compositorState.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    renderer::SourceProgram compositor(programs, 2, compositorState);
    tests::require(compositor.rebuild(compiler, {
        .vertex = layer_request(rhi::ShaderStage::vertex, "fullscreen_vertex"),
        .fragment = layer_request(rhi::ShaderStage::fragment, "composite_fragment", true),
    }));
    tests::LayerComposite layer(device, renderer, bindings, 1, 2);
    layer.run_proof();
}

void wait_graphics(rhi::Device& device, rhi::CommandList& list) {
    auto fence = tests::require(device.create_fence());
    auto queue = tests::require(device.queue(rhi::QueueKind::graphics));
    const std::array<rhi::CommandList*, 1> lists{&list};
    tests::require(queue.submit({.commandLists = lists, .signalFence = &fence,
                                 .signalFenceValue = 1}));
    tests::require(fence.wait(1, std::chrono::seconds(10)));
}

rhi::Texture target(rhi::Device& device, rhi::Extent2D extent) {
    auto color = tests::require(device.create_texture({
        .extent = {extent.width, extent.height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::undefined,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    wait_graphics(device, list);
    return color;
}

using Pixel = std::array<int, 4>;

// Unorm blend results may round either way at .5 boundaries; allow one step.
template <typename Expected>
void check_pixels(rhi::Device& device, rhi::Texture& color, const char* label,
                  Expected expected) {
    const auto extent = color.desc().extent;
    const std::size_t pitch = 256;
    auto output = tests::require(device.create_buffer({
        .size = pitch * extent.height, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::color_attachment,
                         rhi::TextureLayout::transfer_source);
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_texture_to_buffer(color, output, {
        .layout = {.bytesPerRow = pitch, .rowsPerImage = extent.height},
        .texture = {.extent = extent},
    }));
    tests::require(copy.end());
    tests::color_barrier(list, color, rhi::TextureLayout::transfer_source,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    wait_graphics(device, list);
    std::vector<std::byte> bytes(pitch * extent.height);
    tests::require(output.read(0, bytes));
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const Pixel want = expected(x, y);
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const int actual = std::to_integer<int>(bytes[y * pitch + x * 4 + channel]);
                if (std::abs(actual - want[channel]) > 1) {
                    throw std::runtime_error(std::string(label) + ": pixel " +
                        std::to_string(x) + "," + std::to_string(y) + " channel " +
                        std::to_string(channel) + " is " + std::to_string(actual) +
                        ", expected " + std::to_string(want[channel]));
                }
            }
        }
    }
    std::cout << label << ": " << extent.width * extent.height << " pixels verified\n";
}

std::array<QuadVertex, 4> quad(float left, float right, float uRight, float alpha) {
    return {{
        {{left, -1}, {0, 1}, {1, 1, 1, alpha}},
        {{right, -1}, {uRight, 1}, {1, 1, 1, alpha}},
        {{left, 1}, {0, 0}, {1, 1, 1, alpha}},
        {{right, 1}, {uRight, 0}, {1, 1, 1, alpha}},
    }};
}

int run() {
    auto instance = tests::require(rhi::create_metal_instance());
    auto adapter = tests::require(instance.adapter(0));
    auto device = tests::require(adapter.request_device());

    auto bindings = tests::require(renderer::TextureBindingLayout::create(device));

    // 2x1 atlas: white | red; nearest sampling makes each half one texel.
    const std::array<std::byte, 8> texels{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto atlas = tests::require(renderer::SampledTexture::upload(device, {
        .extent = {2, 1}, .bytes = texels,
    }));
    auto binding = tests::require(renderer::TextureBinding::create(device, bindings, atlas));

    rhi::PipelineDesc state;
    state.layout = &bindings.pipeline_layout();
    state.vertexBuffers = {{
        .stride = sizeof(QuadVertex),
        .attributes = {{0, rhi::VertexFormat::float32x2, offsetof(QuadVertex, coordinate)},
                       {1, rhi::VertexFormat::float32x2, offsetof(QuadVertex, uv)},
                       {2, rhi::VertexFormat::float32x4, offsetof(QuadVertex, tint)}},
    }};
    state.colorTargets = {{
        .format = rhi::TextureFormat::rgba8_unorm,
        .blend = {.enabled = true,
                  .color = {rhi::BlendFactor::source_alpha,
                            rhi::BlendFactor::one_minus_source_alpha},
                  .alpha = {rhi::BlendFactor::one, rhi::BlendFactor::one_minus_source_alpha}},
    }};
    state.dynamicState = rhi::DynamicState::scissor;
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    shader::Compiler compiler;
    renderer::SourceProgram program(programs, 1, state);
    tests::require(program.rebuild(compiler, {
        .vertex = request(rhi::ShaderStage::vertex),
        .fragment = request(rhi::ShaderStage::fragment),
    }));

    auto pacer = tests::require(renderer::FramePacer::create(device, 2));
    auto stream = tests::require(renderer::StreamBuffer::create(device, {.initialBytes = 64}));
    auto color = target(device, {16, 16});
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 1, 3};

    struct Draw {
        std::array<QuadVertex, 4> vertices;
        std::optional<rhi::ScissorRect> scissor;
    };
    const auto frame = [&](std::vector<Draw> draws) {
        const auto slot = tests::require(pacer.begin_frame());
        tests::require(stream.begin_frame(slot));
        std::vector<renderer::RenderBatch> batches;
        for (const auto& draw : draws) {
            renderer::RenderBatch batch;
            batch.material = 1;
            batch.instanceCount = 1;
            batch.kind = renderer::DrawKind::Indexed;
            batch.indexFormat = rhi::IndexFormat::uint16;
            batch.vertexCount = static_cast<std::uint32_t>(indices.size());
            batch.vertexElementCount = 4;
            batch.bindings[0] = tests::require(stream.write(std::as_bytes(std::span{draw.vertices})));
            batch.indexBuffer = tests::require(stream.write(std::as_bytes(std::span{indices}), 4));
            batch.bindGroups = {binding.batch_group()};
            batch.scissor = draw.scissor;
            batches.push_back(std::move(batch));
        }
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{
                .extent = {16, 16},
                .colorAttachments = {{.texture = &color, .clear = {0, 0, 1, 1}}},
            },
            std::move(batches)));
        const auto status = renderer.render(graph, nullptr, pacer.signal());
        pacer.end_frame(renderer.last_frame_stats().submitted);
        tests::require(status);
        tests::require(pacer.wait_idle());
    };

    // Alpha 0.2 over blue: white texel -> (51,51,255), red texel -> (51,0,204).
    frame({{quad(-1, 1, 1, 0.2F), std::nullopt}});
    check_pixels(device, color, "alpha blending with sampled atlas texels", [](auto x, auto) {
        return x < 8 ? Pixel{51, 51, 255, 255} : Pixel{51, 0, 204, 255};
    });

    // Atlas update after upload, a clip rectangle and a fully clipped batch.
    const std::array<std::byte, 4> green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    tests::require(atlas.update(device, {1, 0, 0}, {1, 1}, green));
    frame({{quad(-1, 1, 1, 1), rhi::ScissorRect{8, 0, 8, 8}},
           {quad(-1, 1, 1, 1), rhi::ScissorRect{100, 100, 4, 4}}});
    if (renderer.last_frame_stats().renderBatchesClipped != 1) {
        throw std::runtime_error("fully clipped batch was not skipped");
    }
    check_pixels(device, color, "clip rectangle and texture update", [](auto x, auto y) {
        return x >= 8 && y < 8 ? Pixel{0, 255, 0, 255} : Pixel{0, 0, 255, 255};
    });

    // Rebuilt geometry reuses slot 0 after its fence value; the stream grows.
    frame({{quad(-1, 0, 0.5F, 1), std::nullopt},
           {quad(0, 1, 1, 1), rhi::ScissorRect{8, 8, 8, 8}}});
    // The right quad maps the whole atlas over x 8..15 inside its bottom clip.
    check_pixels(device, color, "streamed geometry across paced frames", [](auto x, auto y) {
        if (x < 8) {
            return Pixel{255, 255, 255, 255};
        }
        if (y < 8) {
            return Pixel{0, 0, 255, 255};
        }
        return x < 12 ? Pixel{255, 255, 255, 255} : Pixel{0, 255, 0, 255};
    });
    render_to_texture(device);
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "Renderer GUI primitives Metal proof failed: " << error.what() << '\n';
        return 1;
    }
}

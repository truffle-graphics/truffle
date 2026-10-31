#include "truffle/gui/painter.hpp"

#include "truffle/renderer/frame_stream.hpp"
#include "truffle_gui_shaders.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status failure(base::StatusCode code, std::string message) {
    return base::Status::failure(code, std::move(message));
}

[[nodiscard]] base::Result<rhi::ShaderPackage> metal_package() {
    const std::string_view source = detail::gui_metal_source;
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    rhi::ShaderPackageDesc desc;
    desc.name = "Truffle GUI";
    desc.sources = {{.path = "truffle/gui/shaders/gui.metal",
                     .language = rhi::ShaderSourceLanguage::msl,
                     .sha256 = detail::gui_metal_sha256}};
    desc.compilers = {{.name = "Metal native source", .version = "host SDK"}};
    const auto add = [&](rhi::ShaderStage stage, const char* entry,
                         std::vector<rhi::ResourceBinding> bindings) {
        desc.variants.push_back({
            .target = rhi::ShaderTarget::metal_source,
            .format = rhi::ShaderByteFormat::native_source,
            .kind = rhi::ShaderVariantKind::native_override,
            .stage = stage,
            .entryPoint = entry,
            .reflection = {.bindings = std::move(bindings)},
            .code = {bytes.begin(), bytes.end()},
        });
    };
    add(rhi::ShaderStage::vertex, "gui_vertex", {});
    add(rhi::ShaderStage::fragment, "gui_fragment",
        {{.name = "image", .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::texture, .group = 0, .binding = 0},
         {.name = "image_sampler", .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::sampler, .group = 0, .binding = 1}});
    auto package = rhi::ShaderPackage::create(std::move(desc));
    if (!package.ok()) {
        return package.status();
    }
    return std::move(package).value();
}

} // namespace

struct Painter::State {
    PainterDesc desc;
    renderer::TextureBindingLayout layout;
    renderer::SampledTexture white;
    renderer::TextureBinding white_binding;
    std::unordered_map<std::uint64_t, renderer::TextureBinding> textures;
    std::uint64_t next_texture = 1;
    rhi::Device* device = nullptr;
    renderer::StreamBuffer stream;
    DrawData draw;
    PaintStats stats;
};

Painter::Painter() = default;
Painter::~Painter() = default;
Painter::Painter(Painter&&) noexcept = default;
Painter& Painter::operator=(Painter&&) noexcept = default;

base::Result<Painter> Painter::create(rhi::Device& device, renderer::PipelineCache& programs,
                                      const PainterDesc& desc) {
    if (device.adapter_info().backend != rhi::BackendKind::metal) {
        return failure(base::StatusCode::unsupported,
                       "Truffle GUI drawing currently ships Metal programs only");
    }
    auto state = std::make_unique<State>();
    state->desc = desc;
    state->device = &device;

    auto layout = renderer::TextureBindingLayout::create(device);
    if (!layout.ok()) {
        return layout.status();
    }
    state->layout = std::move(layout).value();
    constexpr std::array<std::byte, 4> white{std::byte{255}, std::byte{255},
                                             std::byte{255}, std::byte{255}};
    auto texture = renderer::SampledTexture::upload(
        device, {.extent = {1, 1}, .bytes = white, .debugName = "Truffle GUI white"});
    if (!texture.ok()) {
        return texture.status();
    }
    state->white = std::move(texture).value();
    auto binding = renderer::TextureBinding::create(device, state->layout, state->white);
    if (!binding.ok()) {
        return binding.status();
    }
    state->white_binding = std::move(binding).value();
    auto stream = renderer::StreamBuffer::create(
        device, {.usage = rhi::BufferUsage::vertex | rhi::BufferUsage::index,
                 .framesInFlight = desc.frames_in_flight});
    if (!stream.ok()) {
        return stream.status();
    }
    state->stream = std::move(stream).value();

    auto package = metal_package();
    if (!package.ok()) {
        return package.status();
    }
    renderer::ProgramDesc program;
    program.vertex = {&package.value(), rhi::ShaderTarget::metal_source, "gui_vertex",
                      rhi::ShaderStage::vertex, {}};
    program.fragment = {&package.value(), rhi::ShaderTarget::metal_source, "gui_fragment",
                        rhi::ShaderStage::fragment, {}};
    program.pipeline.layout = &state->layout.pipeline_layout();
    program.pipeline.vertexBuffers = {{
        .stride = sizeof(ShapeVertex),
        .attributes = {{0, rhi::VertexFormat::float32x2, offsetof(ShapeVertex, position)},
                       {1, rhi::VertexFormat::float32x2, offsetof(ShapeVertex, uv)},
                       {2, rhi::VertexFormat::float32x4, offsetof(ShapeVertex, fill)},
                       {3, rhi::VertexFormat::float32x4, offsetof(ShapeVertex, border)},
                       {4, rhi::VertexFormat::float32x2, offsetof(ShapeVertex, local)},
                       {5, rhi::VertexFormat::float32x4, offsetof(ShapeVertex, shape)}},
    }};
    // The fragment stage writes premultiplied alpha.
    program.pipeline.colorTargets = {{
        .format = desc.color_format,
        .blend = {.enabled = true,
                  .color = {rhi::BlendFactor::one, rhi::BlendFactor::one_minus_source_alpha},
                  .alpha = {rhi::BlendFactor::one, rhi::BlendFactor::one_minus_source_alpha}},
    }};
    program.pipeline.dynamicState = rhi::DynamicState::scissor;
    program.pipeline.debugName = "Truffle GUI";
    if (auto status = programs.register_program(desc.material, program, desc.variant_hash);
        !status.ok()) {
        return status;
    }

    Painter painter;
    painter.state_ = std::move(state);
    return painter;
}

base::Result<TextureId> Painter::add_texture(renderer::SampledTexture& texture,
                                             const rhi::SamplerDesc& sampler) {
    auto binding = renderer::TextureBinding::create(*state_->device, state_->layout, texture,
                                                    sampler);
    if (!binding.ok()) {
        return binding.status();
    }
    const std::uint64_t id = state_->next_texture++;
    state_->textures.emplace(id, std::move(binding).value());
    return TextureId{id};
}

base::Status Painter::remove_texture(TextureId texture) {
    if (state_->textures.erase(texture.value) == 0) {
        return failure(base::StatusCode::invalid_argument,
                       "GUI texture is not registered with this painter");
    }
    return base::Status::success();
}

base::Result<std::vector<renderer::RenderBatch>> Painter::record(
    const Context& context, std::uint32_t slot, rhi::Extent2D target, float scale) {
    State& state = *state_;
    state.stats = {};
    assemble(context, {.target = target, .scale = scale, .clip_y_up = true}, state.draw);
    const DrawData& draw = state.draw;
    state.stats.elements = draw.elements;
    state.stats.clipped = draw.clipped;

    std::vector<renderer::TextureBinding*> bindings;
    bindings.reserve(draw.commands.size());
    for (const DrawCommand& command : draw.commands) {
        if (!command.texture.valid()) {
            bindings.push_back(&state.white_binding);
            continue;
        }
        const auto found = state.textures.find(command.texture.value);
        if (found == state.textures.end()) {
            return failure(base::StatusCode::invalid_argument,
                           "a GUI decoration uses a texture that is not registered");
        }
        bindings.push_back(&found->second);
    }

    if (auto status = state.stream.begin_frame(slot); !status.ok()) {
        return status;
    }
    std::vector<renderer::RenderBatch> batches;
    if (draw.indices.empty()) {
        return batches;
    }
    auto vertices = state.stream.write(std::as_bytes(std::span{draw.vertices}));
    if (!vertices.ok()) {
        return vertices.status();
    }
    auto indices = state.stream.write(std::as_bytes(std::span{draw.indices}), 4);
    if (!indices.ok()) {
        return indices.status();
    }

    batches.reserve(draw.commands.size());
    for (std::size_t i = 0; i < draw.commands.size(); ++i) {
        const DrawCommand& command = draw.commands[i];
        renderer::RenderBatch batch;
        batch.material = state.desc.material;
        batch.variantHash = state.desc.variant_hash;
        batch.instanceCount = 1;
        batch.kind = renderer::DrawKind::Indexed;
        batch.indexFormat = rhi::IndexFormat::uint32;
        batch.vertexCount = command.index_count;
        batch.vertexElementCount = static_cast<std::uint32_t>(draw.vertices.size());
        batch.bindings[0] = vertices.value();
        batch.indexBuffer = {indices.value().buffer,
                             indices.value().offset + command.first_index * sizeof(std::uint32_t),
                             command.index_count * sizeof(std::uint32_t)};
        batch.bindGroups = {bindings[i]->batch_group()};
        batch.scissor = command.clip;
        batches.push_back(std::move(batch));
    }
    state.stats.batches = batches.size();
    return batches;
}

const PaintStats& Painter::last_stats() const noexcept {
    return state_->stats;
}

} // namespace truffle::gui

#pragma once

// Backend-neutral render -> sample proof (#215). The writer program fills its
// color target with opaque red; the compositor samples a 16x16 layer at
// fragment coordinates (binding 0 texture, binding 1 sampler, group 0).
#include "renderer_native_test_utils.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace truffle::tests {

struct LayerBindings {
    rhi::BindGroupLayout groupLayout;
    rhi::PipelineLayout pipelineLayout;
};

inline LayerBindings make_layer_bindings(rhi::Device& device) {
    LayerBindings result;
    result.groupLayout = require(device.create_bind_group_layout({
        .entries = {
            {.binding = 0, .type = rhi::BindingType::sampled_texture,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 1, .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment},
        },
    }));
    result.pipelineLayout = require(device.create_pipeline_layout(
        {.bindGroupLayouts = {&result.groupLayout}}));
    return result;
}

inline rhi::TextureBarrier layer_transition(rhi::Texture& texture,
                                            rhi::TextureLayout from,
                                            rhi::TextureLayout to) {
    const auto stage = [](rhi::TextureLayout layout) {
        return layout == rhi::TextureLayout::undefined ? rhi::PipelineStage::top
             : layout == rhi::TextureLayout::shader_read_only
                 ? rhi::PipelineStage::fragment_shader
                 : rhi::PipelineStage::color_attachment_output;
    };
    const auto access = [](rhi::TextureLayout layout) {
        return layout == rhi::TextureLayout::undefined ? rhi::Access::none
             : layout == rhi::TextureLayout::shader_read_only
                 ? rhi::Access::shader_read
                 : rhi::Access::color_attachment_write;
    };
    return {.texture = &texture,
            .oldLayout = from,
            .newLayout = to,
            .sourceStages = stage(from),
            .destinationStages = stage(to),
            .sourceAccess = access(from),
            .destinationAccess = access(to)};
}

class LayerComposite {
public:
    LayerComposite(rhi::Device& device, renderer::Renderer& renderer,
                   LayerBindings& bindings, renderer::MaterialId writer,
                   renderer::MaterialId compositor)
        : device_(&device), renderer_(&renderer), writer_(writer),
          compositor_(compositor) {
        layer_ = require(device.create_texture({
            .extent = {16, 16, 1},
            .format = rhi::TextureFormat::rgba8_unorm,
            .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::sampled,
        }));
        view_ = require(device.create_texture_view(
            layer_, {.format = rhi::TextureFormat::rgba8_unorm}));
        sampler_ = require(device.create_sampler({}));
        arena_ = require(device.create_descriptor_arena());
        group_ = require(device.create_bind_group({
            .layout = &bindings.groupLayout, .arena = &arena_,
            .entries = {{.binding = 0, .textureView = &view_},
                        {.binding = 1, .sampler = &sampler_}},
        }));
        target_ = make_color_target(device);
        fence_ = require(device.create_fence());
    }
    LayerComposite(const LayerComposite&) = delete;
    LayerComposite& operator=(const LayerComposite&) = delete;

    // Optionally re-render the layer (cleared, then the writer clipped), then
    // composite it over a black target. Layouts are stated by the caller.
    void frame(std::optional<std::array<float, 4>> layerClear,
               std::optional<rhi::ScissorRect> writerClip = std::nullopt) {
        constexpr std::uint64_t layerResource = 1;
        renderer::FrameGraph graph;
        const bool renderLayer = layerClear.has_value();
        if (renderLayer) {
            renderer::RenderBatch writer;
            writer.material = writer_;
            writer.instanceCount = 1;
            writer.scissor = writerClip;
            auto pass = std::make_unique<renderer::RenderPassNode>(
                rhi::RenderPassDesc{
                    .extent = {16, 16},
                    .colorAttachments = {{.texture = &layer_,
                                          .clear = {(*layerClear)[0], (*layerClear)[1],
                                                    (*layerClear)[2], (*layerClear)[3]}}},
                },
                std::vector<renderer::RenderBatch>{writer});
            pass->transition_before(layer_transition(layer_, layerLayout_,
                                                     rhi::TextureLayout::color_attachment));
            const auto id = graph.add_node(std::move(pass));
            require(graph.add_resource_usage(
                id, {layerResource, renderer::ResourceAccess::Write}));
        }
        renderer::RenderBatch compositor;
        compositor.material = compositor_;
        compositor.instanceCount = 1;
        compositor.bindGroups = {{.group = 0, .bindings = &group_}};
        auto pass = std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{
                .extent = {16, 16},
                .colorAttachments = {{.texture = &target_, .clear = {0, 0, 0, 1}}},
            },
            std::vector<renderer::RenderBatch>{compositor});
        if (renderLayer) {
            pass->transition_before(layer_transition(
                layer_, rhi::TextureLayout::color_attachment,
                rhi::TextureLayout::shader_read_only));
            layerLayout_ = rhi::TextureLayout::shader_read_only;
        }
        const auto id = graph.add_node(std::move(pass));
        require(graph.add_resource_usage(id, {layerResource, renderer::ResourceAccess::Read}));
        ++signal_;
        require(renderer_->render(graph, nullptr, {.fence = &fence_, .value = signal_}));
        require(fence_.wait(signal_, std::chrono::seconds(10)));
        const auto& stats = renderer_->last_frame_stats();
        if (stats.renderNodesExecuted != (renderLayer ? 2U : 1U) ||
            stats.renderBatchesExecuted != (renderLayer ? 2U : 1U)) {
            throw std::runtime_error("layer composite executed unexpected work");
        }
    }

    void check(const char* label,
               const std::function<std::array<std::byte, 4>(std::uint32_t)>& expected) {
        constexpr std::size_t pitch = 256;
        auto readback = require(device_->create_buffer({
            .size = pitch * 16, .usage = rhi::BufferUsage::copy_destination,
            .memory = rhi::MemoryDomain::readback,
        }));
        auto pool = require(device_->create_command_pool(rhi::QueueKind::graphics));
        auto list = require(pool.allocate());
        require(list.begin());
        color_barrier(list, target_, rhi::TextureLayout::color_attachment,
                      rhi::TextureLayout::transfer_source);
        auto copy = require(list.begin_copy());
        require(copy.copy_texture_to_buffer(
            target_, readback,
            {.layout = {.bytesPerRow = pitch, .rowsPerImage = 16},
             .texture = {.extent = {16, 16, 1}}}));
        require(copy.end());
        color_barrier(list, target_, rhi::TextureLayout::transfer_source,
                      rhi::TextureLayout::color_attachment);
        require(list.end());
        auto queue = require(device_->queue(rhi::QueueKind::graphics));
        const std::array<rhi::CommandList*, 1> lists{&list};
        ++signal_;
        require(queue.submit({.commandLists = lists, .signalFence = &fence_,
                              .signalFenceValue = signal_}));
        require(fence_.wait(signal_, std::chrono::seconds(10)));
        std::vector<std::byte> bytes(pitch * 16);
        require(readback.read(0, bytes));
        for (std::uint32_t y = 0; y < 16; ++y) {
            for (std::uint32_t x = 0; x < 16; ++x) {
                const auto want = expected(x);
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    if (bytes[y * pitch + x * 4 + channel] != want[channel]) {
                        throw std::runtime_error(std::string(label) + ": pixel mismatch at " +
                                                 std::to_string(x) + "," + std::to_string(y));
                    }
                }
            }
        }
        std::cout << label << ": exact 256 pixels\n";
    }

    // Initial render, re-render after sampling, then reuse without re-rendering.
    void run_proof() {
        constexpr std::array red{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
        constexpr std::array green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
        const auto split = [&](std::uint32_t x) { return x < 8 ? red : green; };
        frame(std::array{0.0F, 0.0F, 1.0F, 1.0F});
        check("layer rendered then sampled", [&](std::uint32_t) { return red; });
        frame(std::array{0.0F, 1.0F, 0.0F, 1.0F}, rhi::ScissorRect{0, 0, 8, 16});
        check("sampled layer re-rendered and sampled again", split);
        frame(std::nullopt);
        check("cached layer sampled without re-rendering", split);
    }

private:
    rhi::Device* device_;
    renderer::Renderer* renderer_;
    renderer::MaterialId writer_;
    renderer::MaterialId compositor_;
    rhi::Texture layer_;
    rhi::TextureView view_;
    rhi::Sampler sampler_;
    rhi::DescriptorArena arena_;
    rhi::BindGroup group_;
    rhi::Texture target_;
    rhi::Fence fence_;
    rhi::TextureLayout layerLayout_ = rhi::TextureLayout::undefined;
    std::uint64_t signal_ = 0;
};

} // namespace truffle::tests

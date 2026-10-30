#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/renderer/texture.hpp"

#include <array>
#include <vector>

namespace {

using namespace truffle;

// Shaderless Null pipeline whose only property under test is dynamic scissor.
class ScissorCache final : public renderer::IPipelineCache {
public:
    ScissorCache(rhi::Device& device, bool dynamic) : device_(&device), dynamic_(dynamic) {}
    base::Status prepare_render_pass(const rhi::RenderPassDesc&) override {
        return base::Status::success();
    }
    base::Result<rhi::Pipeline*> get_or_create(const renderer::InstanceLayout&,
                                               renderer::MaterialId, std::size_t) override {
        if (!pipeline_.valid()) {
            rhi::PipelineDesc desc;
            desc.colorTargets.push_back({.format = rhi::TextureFormat::rgba8_unorm});
            desc.dynamicState = dynamic_ ? rhi::DynamicState::scissor : rhi::DynamicState::none;
            auto result = device_->create_pipeline(desc);
            if (!result.ok()) {
                return result.status();
            }
            pipeline_ = std::move(result).value();
        }
        return &pipeline_;
    }
    void invalidate(renderer::MaterialId) override {}
    void invalidate_all() override { pipeline_ = {}; }

private:
    rhi::Device* device_;
    bool dynamic_;
    rhi::Pipeline pipeline_;
};

renderer::FrameGraph pass_with(rhi::Texture& target, std::vector<renderer::RenderBatch> batches) {
    renderer::FrameGraph graph;
    rhi::RenderPassDesc pass{.extent = {16, 8}};
    pass.colorAttachments.push_back({.texture = &target});
    graph.add_node(std::make_unique<renderer::RenderPassNode>(pass, std::move(batches)));
    return graph;
}

std::vector<std::byte> bytes(std::size_t count, std::byte value) {
    return std::vector<std::byte>(count, value);
}

} // namespace

int main() {
    auto context = tests::make_null_context();
    auto& device = context.device;
    auto target = device.create_texture({
        .extent = {16, 8, 1}, .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment,
    });
    TRUFFLE_CHECK(target.ok());

    // Clip rectangles: intersected with the pass, empty ones skip the draw.
    renderer::RenderBatch inside;
    inside.instanceCount = 1;
    inside.scissor = rhi::ScissorRect{-4, 2, 8, 100};
    renderer::RenderBatch outside = inside;
    outside.scissor = rhi::ScissorRect{16, 0, 4, 4};
    renderer::RenderBatch unclipped;
    unclipped.instanceCount = 1;
    {
        ScissorCache cache(device, true);
        renderer::Renderer renderer(device, &cache);
        auto fence = device.create_fence();
        TRUFFLE_CHECK(fence.ok());
        const auto graph = pass_with(target.value(), {inside, outside, unclipped});
        TRUFFLE_CHECK(renderer.render(graph, nullptr, {&fence.value(), 3}).ok());
        const auto& stats = renderer.last_frame_stats();
        TRUFFLE_CHECK(stats.renderBatchesExecuted == 2);
        TRUFFLE_CHECK(stats.renderBatchesClipped == 1);
        TRUFFLE_CHECK(stats.submitted);
        TRUFFLE_CHECK(fence.value().wait(3, std::chrono::seconds(1)).ok());
        TRUFFLE_CHECK(!renderer.render(graph, nullptr, {&fence.value(), 0}).ok());
        TRUFFLE_CHECK(!renderer.last_frame_stats().submitted);
    }
    {
        ScissorCache cache(device, false);
        renderer::Renderer renderer(device, &cache);
        TRUFFLE_CHECK(renderer.render(pass_with(target.value(), {unclipped})).ok());
        TRUFFLE_CHECK(!renderer.render(pass_with(target.value(), {inside})).ok());
    }

    // Frame pacing over one fence.
    TRUFFLE_CHECK(!renderer::FramePacer::create(device, 0).ok());
    auto pacerResult = renderer::FramePacer::create(device, 2);
    TRUFFLE_CHECK(pacerResult.ok());
    auto pacer = std::move(pacerResult).value();
    TRUFFLE_CHECK(pacer.signal().fence == nullptr);
    TRUFFLE_CHECK(pacer.wait_idle().ok());
    {
        ScissorCache cache(device, true);
        renderer::Renderer renderer(device, &cache);
        const auto graph = pass_with(target.value(), {unclipped});
        for (std::uint32_t frame = 0; frame < 5; ++frame) {
            auto slot = pacer.begin_frame();
            TRUFFLE_CHECK(slot.ok() && slot.value() == frame % 2);
            TRUFFLE_CHECK(!pacer.begin_frame().ok());
            TRUFFLE_CHECK(pacer.signal().value == frame + 1);
            // Frame 2 is not submitted; later waits must not depend on it.
            const bool submit = frame != 2;
            if (submit) {
                TRUFFLE_CHECK(renderer.render(graph, nullptr, pacer.signal()).ok());
            }
            pacer.end_frame(submit && renderer.last_frame_stats().submitted);
        }
        TRUFFLE_CHECK(pacer.wait_idle().ok());
    }

    // Streamed bytes: aligned, per-slot, growing without invalidating views.
    TRUFFLE_CHECK(!renderer::StreamBuffer::create(device, {.initialBytes = 0}).ok());
    auto streamResult = renderer::StreamBuffer::create(device, {.initialBytes = 32});
    TRUFFLE_CHECK(streamResult.ok());
    auto stream = std::move(streamResult).value();
    const auto first = bytes(20, std::byte{1});
    TRUFFLE_CHECK(!stream.write(first).ok());
    TRUFFLE_CHECK(!stream.begin_frame(2).ok());
    TRUFFLE_CHECK(stream.begin_frame(0).ok());
    TRUFFLE_CHECK(!stream.write({}).ok());
    TRUFFLE_CHECK(!stream.write(first, 3).ok());
    auto a = stream.write(first);
    TRUFFLE_CHECK(a.ok() && a.value().offset == 0 && a.value().size == 20);
    const auto second = bytes(8, std::byte{2});
    auto b = stream.write(second, 4);
    TRUFFLE_CHECK(b.ok() && b.value().buffer == a.value().buffer && b.value().offset == 20);
    const auto large = bytes(100, std::byte{3});
    auto c = stream.write(large);
    TRUFFLE_CHECK(c.ok() && c.value().buffer != a.value().buffer && c.value().offset == 0);
    TRUFFLE_CHECK(c.value().buffer->desc().size >= 100);
    std::array<std::byte, 8> readBack{};
    TRUFFLE_CHECK(b.value().buffer->read(20, readBack).ok());
    TRUFFLE_CHECK(readBack[0] == std::byte{2} && readBack[7] == std::byte{2});
    TRUFFLE_CHECK(stream.begin_frame(1).ok());
    auto other = stream.write(first);
    TRUFFLE_CHECK(other.ok() && other.value().buffer != a.value().buffer &&
                   other.value().buffer != c.value().buffer);
    TRUFFLE_CHECK(stream.begin_frame(0).ok());
    auto reused = stream.write(large);
    TRUFFLE_CHECK(reused.ok() && reused.value().buffer == c.value().buffer &&
                   reused.value().offset == 0);

    // Sampled textures: validation of formats, regions and byte layouts.
    const auto pixels = bytes(4 * 4 * 4, std::byte{0x7f});
    TRUFFLE_CHECK(!renderer::SampledTexture::upload(device, {.extent = {0, 4}, .bytes = pixels}).ok());
    TRUFFLE_CHECK(!renderer::SampledTexture::upload(
        device, {.extent = {4, 4}, .format = rhi::TextureFormat::bc1_rgba_unorm, .bytes = pixels}).ok());
    TRUFFLE_CHECK(!renderer::SampledTexture::upload(
        device, {.extent = {4, 4}, .bytes = std::span(pixels).first(60)}).ok());
    TRUFFLE_CHECK(!renderer::SampledTexture::upload(
        device, {.extent = {4, 4}, .bytes = pixels, .bytesPerRow = 8}).ok());
    auto texture = renderer::SampledTexture::upload(device, {.extent = {4, 4}, .bytes = pixels});
    TRUFFLE_CHECK(texture.ok());
    TRUFFLE_CHECK(texture.value().view().valid());
    TRUFFLE_CHECK(texture.value().update(device, {2, 2, 0}, {2, 2}, pixels).ok());
    TRUFFLE_CHECK(!texture.value().update(device, {3, 0, 0}, {2, 1}, pixels).ok());
    TRUFFLE_CHECK(!texture.value().update(device, {0, 0, 1}, {1, 1}, pixels).ok());
    renderer::SampledTexture empty;
    TRUFFLE_CHECK(!empty.update(device, {}, {1, 1}, pixels).ok());
    const auto atlas = bytes(3 * 2, std::byte{9});
    TRUFFLE_CHECK(renderer::SampledTexture::upload(
        device, {.extent = {3, 2}, .format = rhi::TextureFormat::r8_unorm, .bytes = atlas}).ok());
    return 0;
}

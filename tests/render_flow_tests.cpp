#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/render/renderer.hpp"
#include "truffle/render/frame_graph.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <cstring>

int main() {
    auto context = truffle::tests::make_null_context();

    // Create a 2-frame upload ring with 1 MiB per frame.
    auto ringResult = context.device.create_upload_ring(2, 1024 * 1024);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    // Build an InstanceLayout with one Transform channel (SoA).
    truffle::render::InstanceLayout layout;
    layout.channels.push_back({
        truffle::render::ChannelKind::Transform,
        0, 0, 64,
    });
    layout.bindingModel = truffle::render::BindingModel::Separate;

    // Allocate ring memory for 3 instances × 64 bytes each.
    constexpr std::uint32_t kInstances  = 3;
    constexpr std::size_t   kStride     = 64;
    auto alloc = ring.allocate(kInstances * kStride);
    TRUFFLE_CHECK(alloc.valid());

    // Write identity matrices directly into ring memory (no intermediate copy).
    auto* dst = static_cast<float*>(alloc.mapped);
    for (std::uint32_t i = 0; i < kInstances; ++i) {
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        std::memcpy(dst + i * 16, identity, kStride);
    }

    truffle::render::RenderBatch batch;
    batch.layout        = layout;
    batch.instanceCount = kInstances;
    batch.vertexCount   = 3;
    batch.bindings[0]   = {alloc.buffer, alloc.offset, kInstances * kStride};
    batch.kind          = truffle::render::DrawKind::Direct;

    const truffle::render::RenderBatch batches[] = {batch};

    // --- Headless path (no swapchain) ---
    truffle::render::Renderer headlessRenderer{context.device};
    TRUFFLE_CHECK(headlessRenderer.render([&]() { truffle::render::FrameGraph fg; fg.add_node(std::make_unique<truffle::render::RenderPassNode>(true, std::vector<truffle::render::RenderBatch>(std::begin(batches), std::end(batches)))); return fg; }()).ok());
    const auto& headlessStats = headlessRenderer.last_frame_stats();
    TRUFFLE_CHECK(headlessStats.computeNodesExecuted == 0);
    TRUFFLE_CHECK(headlessStats.renderNodesExecuted == 1);
    TRUFFLE_CHECK(headlessStats.renderBatchesExecuted == 1);
    TRUFFLE_CHECK(!headlessStats.presented);
    TRUFFLE_CHECK(context.instance.stats().drawsRecorded == 1);
    TRUFFLE_CHECK(context.instance.stats().submissions   == 1);

    // --- Swapchain path ---
    auto surfaceResult = context.device.create_surface({
        .native        = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {640, 480},
    });
    TRUFFLE_CHECK(surfaceResult.ok());
    auto surface = std::move(surfaceResult).value();

    auto swapchainResult = context.device.create_swapchain(
        surface, {.extent = {640, 480}, .imageCount = 2});
    TRUFFLE_CHECK(swapchainResult.ok());
    auto swapchain = std::move(swapchainResult).value();

    truffle::render::Renderer renderer{context.device};
    TRUFFLE_CHECK(renderer.render([&]() { truffle::render::FrameGraph fg; fg.add_node(std::make_unique<truffle::render::RenderPassNode>(true, std::vector<truffle::render::RenderBatch>(std::begin(batches), std::end(batches)))); return fg; }(), &swapchain).ok());
    const auto& swapchainStats = renderer.last_frame_stats();
    TRUFFLE_CHECK(swapchainStats.computeNodesExecuted == 0);
    TRUFFLE_CHECK(swapchainStats.renderNodesExecuted == 1);
    TRUFFLE_CHECK(swapchainStats.renderBatchesExecuted == 1);
    TRUFFLE_CHECK(swapchainStats.presented);

    // Two draws total (headless + swapchain), two submissions
    TRUFFLE_CHECK(context.instance.stats().drawsRecorded == 2);
    TRUFFLE_CHECK(context.instance.stats().submissions   == 2);
    return 0;
}

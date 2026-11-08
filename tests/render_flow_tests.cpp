#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/renderer/specialist/render_batch.hpp"
#include "truffle/renderer/specialist/renderer.hpp"
#include "truffle/renderer/specialist/frame_graph.hpp"
#include "truffle/rhi/null_backend.hpp"

#include <cstring>

int main() {
    auto context = truffle::tests::make_null_context();

    // Create a 2-frame upload ring with 1 MiB per frame.
    auto ringResult = context.device.create_upload_ring(2, 1024 * 1024);
    TFL_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    // Build an InstanceLayout with one Transform channel (SoA).
    truffle::renderer::specialist::InstanceLayout layout;
    layout.channels.push_back({
        truffle::renderer::specialist::ChannelKind::Transform,
        0, 0, 64,
    });
    layout.bindingModel = truffle::renderer::specialist::BindingModel::Separate;

    // Allocate ring memory for 3 instances × 64 bytes each.
    constexpr std::uint32_t kInstances  = 3;
    constexpr std::size_t   kStride     = 64;
    auto alloc = ring.allocate(kInstances * kStride);
    TFL_CHECK(alloc.valid());

    // Write identity matrices directly into ring memory (no intermediate copy).
    auto* dst = static_cast<float*>(alloc.mapped);
    for (std::uint32_t i = 0; i < kInstances; ++i) {
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        std::memcpy(dst + i * 16, identity, kStride);
    }

    truffle::renderer::specialist::RenderBatch batch;
    batch.layout        = layout;
    batch.instanceCount = kInstances;
    batch.vertexCount   = 3;
    batch.bindings[0]   = {alloc.buffer, alloc.offset, kInstances * kStride};
    batch.kind          = truffle::renderer::specialist::DrawKind::Direct;

    const truffle::renderer::specialist::RenderBatch batches[] = {batch};

    // --- Headless path (no swapchain) ---
    truffle::renderer::specialist::Renderer headlessRenderer{context.device};
    TFL_CHECK(headlessRenderer.render([&]() { truffle::renderer::specialist::FrameGraph fg; fg.add_node(std::make_unique<truffle::renderer::specialist::RenderPassNode>(true, std::vector<truffle::renderer::specialist::RenderBatch>(std::begin(batches), std::end(batches)))); return fg; }()).ok());
    const auto& headlessStats = headlessRenderer.last_frame_stats();
    TFL_CHECK(headlessStats.computeNodesExecuted == 0);
    TFL_CHECK(headlessStats.renderNodesExecuted == 1);
    TFL_CHECK(headlessStats.renderBatchesExecuted == 1);
    TFL_CHECK(!headlessStats.presented);
    TFL_CHECK(context.instance.stats().drawsRecorded == 1);
    TFL_CHECK(context.instance.stats().submissions   == 1);

    // --- Swapchain path ---
    auto surfaceResult = context.device.create_surface({
        .native        = {.kind = truffle::rhi::NativeSurfaceKind::headless},
        .initialExtent = {640, 480},
    });
    TFL_CHECK(surfaceResult.ok());
    auto surface = std::move(surfaceResult).value();

    auto swapchainResult = context.device.create_swapchain(
        surface, {.extent = {640, 480}, .imageCount = 2});
    TFL_CHECK(swapchainResult.ok());
    auto swapchain = std::move(swapchainResult).value();

    truffle::renderer::specialist::Renderer renderer{context.device};
    TFL_CHECK(renderer.render([&]() { truffle::renderer::specialist::FrameGraph fg; fg.add_node(std::make_unique<truffle::renderer::specialist::RenderPassNode>(true, std::vector<truffle::renderer::specialist::RenderBatch>(std::begin(batches), std::end(batches)))); return fg; }(), &swapchain).ok());
    const auto& swapchainStats = renderer.last_frame_stats();
    TFL_CHECK(swapchainStats.computeNodesExecuted == 0);
    TFL_CHECK(swapchainStats.renderNodesExecuted == 1);
    TFL_CHECK(swapchainStats.renderBatchesExecuted == 1);
    TFL_CHECK(swapchainStats.presented);

    // Two draws total (headless + swapchain), two submissions
    TFL_CHECK(context.instance.stats().drawsRecorded == 2);
    TFL_CHECK(context.instance.stats().submissions   == 2);
    return 0;
}

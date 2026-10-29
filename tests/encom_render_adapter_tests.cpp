#include "test_support.hpp"
#include "rhi_test_utils.hpp"
#include "truffle/renderer/renderer.hpp"
#include "truffle/renderer/frame_graph.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/encom_renderer/encom_render_adapter.hpp"

int main() {
    // --- Setup backend, device, and upload ring ---
    auto context = truffle::tests::make_null_context();

    auto ringResult = context.device.create_upload_ring(2, 1024 * 1024);
    TRUFFLE_CHECK(ringResult.ok());
    auto ring = std::move(ringResult).value();

    // --- Populate EnCom world ---
    truffle::encom::World world;

    const auto cam = world.create();
    world.emplace<truffle::renderer::Transform>(cam);
    world.emplace<truffle::renderer::Camera>(
        cam, truffle::renderer::Camera{1.0472F, 1.5708F, 0.1F, 5000.0F, 1.7778F});

    const auto light = world.create();
    world.emplace<truffle::renderer::Transform>(light);
    world.emplace<truffle::renderer::Light>(
        light, truffle::renderer::Light{2.0F, 200.0F, {1.0F, 0.9F, 0.8F}});

    // Three mesh instances
    for (int i = 0; i < 3; ++i) {
        const auto mesh = world.create();
        world.emplace<truffle::renderer::Transform>(mesh);
        world.emplace<truffle::renderer::MeshRenderer>(
            mesh, truffle::renderer::MeshRenderer{1, 0, 6, 0});
    }

    // --- Extract via EnComRenderAdapter ---
    truffle::encom_renderer::EnComRenderAdapter adapter;
    auto frame = adapter.extract(world, ring);

    TRUFFLE_CHECK(frame.cameras.size() == 1);
    TRUFFLE_CHECK(frame.lights.size() == 1);
    TRUFFLE_CHECK(frame.meshBatches.size() == 1);

    const auto& batch = frame.meshBatches[0];
    TRUFFLE_CHECK(batch.instanceCount == 3);
    TRUFFLE_CHECK(batch.vertexCount == 6);
    TRUFFLE_CHECK(batch.bindings[0].buffer != nullptr);

    // Verify ring backing pointer is valid (data was written)
    TRUFFLE_CHECK(batch.bindings[0].size == 3 * 64);

    // --- Render the extracted batches ---
    TRUFFLE_CHECK(truffle::renderer::Renderer{context.device}.render([&]() { truffle::renderer::FrameGraph fg; fg.add_node(std::make_unique<truffle::renderer::RenderPassNode>(true, frame.meshBatches)); return fg; }()).ok());
    TRUFFLE_CHECK(context.instance.stats().drawsRecorded == 1);
    TRUFFLE_CHECK(context.instance.stats().submissions == 1);

    return 0;
}

#include "truffle/assets/assets.hpp"
#include "truffle/diagnostics/diagnostics.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/scene/scene_adapter.hpp"

#include <utility>

int main() {
    truffle::assets::GeometryStreamDesc stream;
    stream.id = truffle::assets::AssetId{1};
    stream.attributes.push_back({
        .semantic = truffle::assets::AttributeSemantic::Position,
        .format = truffle::assets::AttributeFormat::Float32x3,
    });
    if (!stream.id.valid()) return 1;

    auto backend = truffle::rhi::create_null_backend();
    auto deviceResult = backend->create_device({});
    if (!deviceResult.ok()) return 1;
    auto device = std::move(deviceResult).value();

    auto ringResult = device->create_upload_ring(2, 256 * 1024);
    if (!ringResult.ok()) return 1;
    auto ring = std::move(ringResult).value();

    truffle::ecs::World world;
    const auto e = world.create();
    world.emplace<truffle::render::Transform>(e);
    world.emplace<truffle::render::MeshRenderer>(e);

    truffle::scene::SceneAdapter adapter;
    auto frame = adapter.extract(world, *ring);
    if (frame.meshBatches.size() != 1) return 1;

    auto summary = truffle::diagnostics::summarize_render_batch(
        frame.meshBatches.front());
    return (summary.instanceCount == 1) ? 0 : 1;
}

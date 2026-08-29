#include "truffle/scene/scene_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace truffle::scene {

namespace {

constexpr std::size_t   kTransformBytes  = 64; // 16 floats × 4 bytes
constexpr std::uint32_t kTransformStride = 64;

render::InstanceLayout make_transform_layout() {
    render::InstanceLayout layout;
    layout.channels.push_back(render::ChannelDesc{
        .kind    = render::ChannelKind::Transform,
        .binding = 0,
        .offset  = 0,
        .stride  = kTransformStride,
    });
    layout.bindingModel = render::BindingModel::Separate;
    return layout;
}

} // namespace

SceneFrame SceneAdapter::extract(ecs::World& world,
                                  rhi::UploadRing& ring) const {
    SceneFrame frame;

    // --- Cameras ---
    world.each<render::Transform, render::Camera>(
        [&frame](ecs::Entity, render::Transform& t, render::Camera& c) {
            frame.cameras.push_back(SceneFrame::CameraState{
                t.worldMatrix,
                c.verticalFieldOfViewRadians,
                c.nearPlane,
                c.farPlane,
                c.aspectRatio,
            });
        });

    // --- Lights ---
    world.each<render::Transform, render::Light>(
        [&frame](ecs::Entity, render::Transform& t, render::Light& l) {
            frame.lights.push_back(SceneFrame::LightState{
                {t.worldMatrix[12], t.worldMatrix[13], t.worldMatrix[14], 1.0F},
                l.intensity,
                l.color,
            });
        });

    // --- Mesh renderers — two passes: count, then write into ring ---
    std::uint32_t meshCount       = 0;
    std::uint32_t firstVertexCount = 3;

    world.each<render::Transform, render::MeshRenderer>(
        [&](ecs::Entity, render::Transform&, render::MeshRenderer& m) {
            ++meshCount;
            firstVertexCount = m.vertexCount;
        });

    if (meshCount > 0) {
        auto alloc = ring.allocate(meshCount * kTransformBytes);
        if (alloc.valid()) {
            auto* dst = static_cast<float*>(alloc.mapped);

            world.each<render::Transform, render::MeshRenderer>(
                [&](ecs::Entity, render::Transform& t, render::MeshRenderer&) {
                    std::memcpy(dst, t.worldMatrix.data(), kTransformBytes);
                    dst += 16;
                });

            render::RenderBatch batch;
            batch.layout        = make_transform_layout();
            batch.instanceCount = meshCount;
            batch.vertexCount   = firstVertexCount;
            batch.bindings[0]   = {alloc.buffer, alloc.offset,
                                   meshCount * kTransformBytes};
            batch.kind          = render::DrawKind::Direct;

            frame.meshBatches.push_back(std::move(batch));
        }
    }

    return frame;
}

} // namespace truffle::scene

#include "truffle/encom_renderer/encom_render_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace truffle::encom_renderer {

namespace {

constexpr std::size_t   kTransformBytes  = 64; // 16 floats × 4 bytes
constexpr std::uint32_t kTransformStride = 64;

renderer::InstanceLayout make_transform_layout() {
    renderer::InstanceLayout layout;
    layout.channels.push_back(renderer::ChannelDesc{
        .kind    = renderer::ChannelKind::Transform,
        .binding = 0,
        .offset  = 0,
        .stride  = kTransformStride,
    });
    layout.bindingModel = renderer::BindingModel::Separate;
    return layout;
}

} // namespace

EnComRenderFrame EnComRenderAdapter::extract(encom::World& world,
                                  rhi::UploadRing& ring) const {
    EnComRenderFrame frame;

    // --- Cameras ---
    world.each<renderer::Transform, renderer::Camera>(
        [&frame](encom::Entity, renderer::Transform& t, renderer::Camera& c) {
            frame.cameras.push_back(EnComRenderFrame::CameraState{
                t.worldMatrix,
                c.verticalFieldOfViewRadians,
                c.nearPlane,
                c.farPlane,
                c.aspectRatio,
            });
        });

    // --- Lights ---
    world.each<renderer::Transform, renderer::Light>(
        [&frame](encom::Entity, renderer::Transform& t, renderer::Light& l) {
            frame.lights.push_back(EnComRenderFrame::LightState{
                {t.worldMatrix[12], t.worldMatrix[13], t.worldMatrix[14], 1.0F},
                l.intensity,
                l.color,
            });
        });

    // --- Mesh renderers — two passes: count, then write into ring ---
    std::uint32_t meshCount       = 0;
    std::uint32_t firstVertexCount = 3;

    world.each<renderer::Transform, renderer::MeshRenderer>(
        [&](encom::Entity, renderer::Transform&, renderer::MeshRenderer& m) {
            ++meshCount;
            firstVertexCount = m.vertexCount;
        });

    if (meshCount > 0) {
        auto alloc = ring.allocate(meshCount * kTransformBytes);
        if (alloc.valid()) {
            auto* dst = static_cast<float*>(alloc.mapped);

            world.each<renderer::Transform, renderer::MeshRenderer>(
                [&](encom::Entity, renderer::Transform& t, renderer::MeshRenderer&) {
                    std::memcpy(dst, t.worldMatrix.data(), kTransformBytes);
                    dst += 16;
                });

            renderer::RenderBatch batch;
            batch.layout        = make_transform_layout();
            batch.instanceCount = meshCount;
            batch.vertexCount   = firstVertexCount;
            batch.bindings[0]   = {alloc.buffer, alloc.offset,
                                   meshCount * kTransformBytes};
            batch.kind          = renderer::DrawKind::Direct;

            frame.meshBatches.push_back(std::move(batch));
        }
    }

    return frame;
}

} // namespace truffle::encom_renderer

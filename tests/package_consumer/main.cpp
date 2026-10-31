#include "truffle/asset_render/asset_render.hpp"
#include "truffle/assets/assets.hpp"
#include "truffle/base/config.hpp"
#include "truffle/base/handle.hpp"
#include "truffle/base/status.hpp"
#include "truffle/base/version.hpp"
#include "truffle/diagnostics/diagnostics.hpp"
#include "truffle/rhi/null_backend.hpp"
#include "truffle/encom_renderer/encom_render_adapter.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/renderer/data_layout.hpp"
#include "truffle/renderer/depth_target.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/renderer/mesh.hpp"
#include "truffle/renderer/texture.hpp"
#include "truffle/renderer/view.hpp"

#include <array>
#include <iostream>
#include <type_traits>
#include <utility>

int main() {
    static_assert(std::is_default_constructible_v<truffle::base::RuntimeConfig>);
    static_assert(std::is_same_v<truffle::rhi::Status, truffle::base::Status>);
    static_assert(truffle::base::kApiVersionPacked ==
                  truffle::base::pack_api_version(truffle::base::kApiVersion));
    struct ConsumerHandleTag {};
    static_assert(!truffle::base::Handle<ConsumerHandleTag>{}.valid());

    truffle::gui::Context gui;
    if (!gui.create(gui.root()).ok()) return 1;

    truffle::assets::GeometryStreamDesc stream;
    stream.id = truffle::assets::AssetId{1};
    stream.attributes.push_back({
        .semantic = truffle::assets::AttributeSemantic::Position,
        .format = truffle::assets::AttributeFormat::Float32x3,
    });
    if (!stream.id.valid()) return 1;

    truffle::assets::MaterialAssetDesc material;
    material.id = truffle::assets::AssetId{2};
    material.requiredAttributes.push_back(
        truffle::assets::AttributeSemantic::Position);

    truffle::assets::MeshAssetDesc mesh;
    mesh.id = truffle::assets::AssetId{3};
    mesh.material = material.id;
    mesh.vertexCount = 1;
    mesh.streams.push_back(stream);

    truffle::asset_render::RenderBatchPlanOptions planning;
    planning.channelMappings.push_back({
        truffle::assets::AttributeSemantic::Position,
        truffle::renderer::ChannelKind::CustomFloat4,
    });
    auto plannedBatch =
        truffle::asset_render::plan_render_batch(mesh, material, planning);
    if (!plannedBatch.ok()) return 1;

    auto instanceResult = truffle::rhi::create_null_instance();
    if (!instanceResult.ok()) return 1;
    auto instance = std::move(instanceResult).value();
    auto adapterResult = instance.adapter(0);
    if (!adapterResult.ok()) return 1;
    auto rhiAdapter = std::move(adapterResult).value();
    auto deviceResult = rhiAdapter.request_device();
    if (!deviceResult.ok()) return 1;
    auto device = std::move(deviceResult).value();

    static_assert(!std::is_copy_constructible_v<truffle::renderer::Mesh>);
    auto projection = truffle::renderer::perspective(1, 1, .1F, 10);
    if (!projection.ok()) {
        std::cerr << projection.status().message << '\n';
        return 1;
    }
    const std::array<float, 12> vertices{-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1};
    const std::array streams{
        truffle::renderer::MeshStream{std::as_bytes(std::span{vertices}), 16},
    };
    auto gpuMesh = truffle::renderer::Mesh::upload(device, {
        .streams = streams, .vertexCount = 3,
    });
    if (!gpuMesh.ok()) {
        std::cerr << gpuMesh.status().message << '\n';
        return 1;
    }
    const std::array<truffle::rhi::VertexBufferLayoutDesc, 1> layouts{{
        {.stride = 16, .attributes = {{.location = 0}}},
    }};
    const auto dataStatus = truffle::renderer::validate_batch_data(
        gpuMesh.value().batch(1), layouts);
    truffle::renderer::DepthTarget depth(device);
    const auto depthStatus = depth.resize({16, 16});
    if (!dataStatus.ok() || !depthStatus.ok()) {
        std::cerr << dataStatus.message << ' ' << depthStatus.message << '\n';
        return 1;
    }

    auto pacer = truffle::renderer::FramePacer::create(device);
    auto streamed = truffle::renderer::StreamBuffer::create(device);
    const std::array<std::byte, 4> texel{};
    auto atlas = truffle::renderer::SampledTexture::upload(device, {
        .extent = {1, 1}, .bytes = texel,
    });
    if (!pacer.ok() || !streamed.ok() || !atlas.ok()) {
        std::cerr << "Renderer GUI primitives unavailable\n";
        return 1;
    }
    const auto slot = pacer.value().begin_frame();
    if (!slot.ok() || !streamed.value().begin_frame(slot.value()).ok() ||
        !streamed.value().write(std::as_bytes(std::span{vertices})).ok()) {
        return 1;
    }
    pacer.value().end_frame(false);

    auto ringResult = device.create_upload_ring(2, 256 * 1024);
    if (!ringResult.ok()) return 1;
    auto ring = std::move(ringResult).value();

    truffle::encom::World world;
    const auto e = world.create();
    world.emplace<truffle::renderer::Transform>(e);
    world.emplace<truffle::renderer::MeshRenderer>(e);

    truffle::encom_renderer::EnComRenderAdapter adapter;
    auto frame = adapter.extract(world, ring);
    if (frame.meshBatches.size() != 1) return 1;

    auto summary = truffle::diagnostics::summarize_render_batch(
        frame.meshBatches.front());
    return (summary.instanceCount == 1) ? 0 : 1;
}

#pragma once

#include "truffle/assets/assets.hpp"
#include "truffle/render/render_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace truffle::asset_render {

struct AttributeChannelMapping {
    assets::AttributeSemantic semantic = assets::AttributeSemantic::Custom;
    render::ChannelKind channel = render::ChannelKind::CustomFloat4;
};

struct RenderBatchPlanOptions {
    render::BindingModel bindingModel = render::BindingModel::Interleaved;
    std::uint32_t baseBinding = 0;
    bool includeOptionalMappedAttributes = false;
    std::vector<AttributeChannelMapping> channelMappings;
};

struct AttributeChannelPlan {
    assets::AssetId stream;
    assets::AttributeDesc attribute;
    render::ChannelDesc channel;
};

struct PlannedBufferBinding {
    std::uint32_t binding = 0;
    assets::AssetId stream;
    assets::GeometryStreamRole role = assets::GeometryStreamRole::Instance;
    std::uint32_t elementCount = 0;
    std::size_t byteSize = 0;
};

struct RenderBatchPlan {
    assets::AssetId mesh;
    assets::AssetId material;
    render::RenderBatch batch;
    std::vector<AttributeChannelPlan> attributes;
    std::vector<PlannedBufferBinding> bindings;
};

struct AssetGroupRenderPlan {
    assets::AssetId group;
    std::vector<RenderBatchPlan> batches;
};

[[nodiscard]] core::Result<RenderBatchPlan> plan_render_batch(
    const assets::MeshAssetDesc& mesh,
    const assets::MaterialAssetDesc& material,
    const RenderBatchPlanOptions& options = {});

[[nodiscard]] core::Result<RenderBatchPlan> plan_catalog_render_batch(
    const assets::AssetCatalog& catalog,
    assets::AssetId mesh,
    const RenderBatchPlanOptions& options = {});

[[nodiscard]] core::Result<AssetGroupRenderPlan> plan_group_render_batches(
    const assets::AssetCatalog& catalog,
    assets::AssetId group,
    const RenderBatchPlanOptions& options = {});

} // namespace truffle::asset_render


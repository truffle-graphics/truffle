#include "truffle/asset_render/asset_render.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace truffle::asset_render {

namespace {

struct LocatedAttribute {
    assets::AssetId stream;
    assets::GeometryStreamRole role = assets::GeometryStreamRole::Instance;
    std::uint32_t elementCount = 0;
    std::size_t byteSize = 0;
    const assets::GeometryStreamDesc* streamDesc = nullptr;
    const assets::AttributeDesc* attribute = nullptr;
};

[[nodiscard]] core::Status failure(core::StatusCode code, const char* message) {
    return core::Status::failure(code, message);
}

[[nodiscard]] bool fits_u32(std::size_t value) noexcept {
    return value <= std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] const AttributeChannelMapping* find_mapping(
    const RenderBatchPlanOptions& options,
    assets::AttributeSemantic semantic) noexcept {
    const auto it = std::find_if(
        options.channelMappings.begin(), options.channelMappings.end(),
        [semantic](const AttributeChannelMapping& mapping) {
            return mapping.semantic == semantic;
        });
    return it == options.channelMappings.end() ? nullptr : &*it;
}

[[nodiscard]] bool has_planned_semantic(
    const std::vector<AttributeChannelPlan>& attributes,
    assets::AttributeSemantic semantic) noexcept {
    return std::any_of(
        attributes.begin(), attributes.end(),
        [semantic](const AttributeChannelPlan& plan) {
            return plan.attribute.semantic == semantic;
        });
}

[[nodiscard]] std::optional<LocatedAttribute> find_attribute(
    const assets::MeshAssetDesc& mesh,
    assets::AttributeSemantic semantic) noexcept {
    for (const auto& stream : mesh.streams) {
        for (const auto& attribute : stream.attributes) {
            if (attribute.semantic == semantic) {
                return LocatedAttribute{
                    .stream = stream.id,
                    .role = stream.role,
                    .elementCount = stream.elementCount,
                    .byteSize = stream.byteSize,
                    .streamDesc = &stream,
                    .attribute = &attribute,
                };
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] core::Result<render::ChannelKind> default_channel_for(
    const assets::AttributeDesc& attribute) {
    using assets::AttributeFormat;
    using assets::AttributeSemantic;
    using render::ChannelKind;

    switch (attribute.semantic) {
    case AttributeSemantic::Transform:
        if (attribute.format != AttributeFormat::Float32x4x4) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: transform attributes must be Float32x4x4");
        }
        return ChannelKind::Transform;
    case AttributeSemantic::LocalTransform:
        if (attribute.format != AttributeFormat::Float32x4x4) {
            return failure(
                core::StatusCode::invalid_argument,
                "AssetRender: local transform attributes must be Float32x4x4");
        }
        return ChannelKind::LocalTransform;
    case AttributeSemantic::Color:
        if (attribute.format != AttributeFormat::Float32x4 &&
            attribute.format != AttributeFormat::UInt8x4Norm) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: color attributes must be Float32x4 or UInt8x4Norm");
        }
        return ChannelKind::Color;
    case AttributeSemantic::Normal:
        if (attribute.format != AttributeFormat::Float32x3) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: normal attributes must be Float32x3");
        }
        return ChannelKind::Normal;
    case AttributeSemantic::TexCoord:
        if (attribute.format != AttributeFormat::Float32x2) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: texcoord attributes must be Float32x2");
        }
        return ChannelKind::TexCoord;
    case AttributeSemantic::ParentIndex:
        if (attribute.format != AttributeFormat::UInt32 &&
            attribute.format != AttributeFormat::SInt32) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: parent index attributes must be UInt32 or SInt32");
        }
        return ChannelKind::ParentIndex;
    case AttributeSemantic::Custom:
        if (attribute.format != AttributeFormat::Float32x4) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: default custom attributes must be Float32x4");
        }
        return ChannelKind::CustomFloat4;
    case AttributeSemantic::Position:
    case AttributeSemantic::Scale:
    case AttributeSemantic::Radius:
    case AttributeSemantic::Velocity:
    case AttributeSemantic::Intensity:
    case AttributeSemantic::Confidence:
    case AttributeSemantic::Classification:
        return failure(
            core::StatusCode::unsupported,
            "AssetRender: attribute semantic needs an explicit channel mapping");
    }

    return failure(core::StatusCode::unsupported,
                   "AssetRender: unsupported attribute semantic");
}

[[nodiscard]] core::Result<render::ChannelKind> channel_for(
    const assets::AttributeDesc& attribute,
    const RenderBatchPlanOptions& options) {
    if (const auto* mapping = find_mapping(options, attribute.semantic);
        mapping != nullptr) {
        return mapping->channel;
    }
    return default_channel_for(attribute);
}

[[nodiscard]] std::size_t declared_stride(
    const assets::GeometryStreamDesc& stream,
    const assets::AttributeDesc& attribute) noexcept {
    return attribute.stride == 0 ? assets::minimum_stride(stream)
                                 : attribute.stride;
}

[[nodiscard]] core::Status add_binding(RenderBatchPlan& plan,
                                       const LocatedAttribute& located,
                                       std::uint32_t binding) {
    const auto existing = std::find_if(
        plan.bindings.begin(), plan.bindings.end(),
        [binding](const PlannedBufferBinding& candidate) {
            return candidate.binding == binding;
        });
    if (existing != plan.bindings.end()) {
        if (existing->stream != located.stream) {
            return failure(
                core::StatusCode::invalid_argument,
                "AssetRender: multiple streams cannot share one binding");
        }
        return core::Status::success();
    }

    plan.bindings.push_back({
        .binding = binding,
        .stream = located.stream,
        .role = located.role,
        .elementCount = located.elementCount,
        .byteSize = located.byteSize,
    });
    return core::Status::success();
}

[[nodiscard]] core::Status add_attribute_plan(
    RenderBatchPlan& plan,
    const LocatedAttribute& located,
    const RenderBatchPlanOptions& options) {
    const auto channelResult = channel_for(*located.attribute, options);
    if (!channelResult.ok()) {
        return channelResult.status();
    }

    const auto formatSize = assets::attribute_format_size(located.attribute->format);
    const auto stride = declared_stride(*located.streamDesc, *located.attribute);
    if (formatSize == 0 || stride == 0 ||
        located.attribute->offset + formatSize > stride) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: attribute offset/stride does not fit the format");
    }
    if (!fits_u32(located.attribute->stream) ||
        !fits_u32(located.attribute->offset) || !fits_u32(stride)) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: channel binding, offset, or stride exceeds uint32 range");
    }

    const auto binding = options.baseBinding + located.attribute->stream;
    if (binding >= render::RenderBatch::kMaxBindings ||
        binding < options.baseBinding) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: channel binding exceeds RenderBatch binding limit");
    }

    auto bindingStatus = add_binding(plan, located, binding);
    if (!bindingStatus.ok()) {
        return bindingStatus;
    }

    render::ChannelDesc channel;
    channel.kind = channelResult.value();
    channel.binding = binding;
    channel.offset = static_cast<std::uint32_t>(located.attribute->offset);
    channel.stride = static_cast<std::uint32_t>(stride);

    plan.batch.layout.channels.push_back(channel);
    plan.attributes.push_back({
        .stream = located.stream,
        .attribute = *located.attribute,
        .channel = channel,
    });
    return core::Status::success();
}

[[nodiscard]] core::Result<std::uint32_t> infer_instance_count(
    const RenderBatchPlan& plan) {
    std::optional<std::uint32_t> count;
    for (const auto& binding : plan.bindings) {
        if (binding.role != assets::GeometryStreamRole::Instance &&
            binding.role != assets::GeometryStreamRole::Custom) {
            continue;
        }
        if (!count.has_value()) {
            count = binding.elementCount;
            continue;
        }
        if (*count != binding.elementCount) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: instance streams disagree on element count");
        }
    }
    return count.value_or(1);
}

[[nodiscard]] std::size_t blend_variant(std::size_t h,
                                        std::size_t value) noexcept {
    return h ^ (value + 0x9e3779b9u + (h << 6u) + (h >> 2u));
}

[[nodiscard]] bool should_plan_optional_attribute(
    const assets::AttributeDesc& attribute,
    const RenderBatchPlanOptions& options) {
    if (find_mapping(options, attribute.semantic) != nullptr) {
        return true;
    }
    return default_channel_for(attribute).ok();
}

} // namespace

core::Result<RenderBatchPlan> plan_render_batch(
    const assets::MeshAssetDesc& mesh,
    const assets::MaterialAssetDesc& material,
    const RenderBatchPlanOptions& options) {
    if (!mesh.id.valid()) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: mesh id must be valid");
    }
    if (!material.id.valid()) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: material id must be valid");
    }
    if (!mesh.material.valid()) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: mesh must reference a valid material");
    }
    if (mesh.material != material.id) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: mesh material does not match supplied material");
    }

    auto requirementStatus =
        assets::validate_material_requirements(material, mesh);
    if (!requirementStatus.ok()) {
        return requirementStatus;
    }

    RenderBatchPlan plan;
    plan.mesh = mesh.id;
    plan.material = material.id;
    plan.batch.layout.bindingModel = options.bindingModel;
    plan.batch.vertexCount = mesh.vertexCount;
    plan.batch.kind = mesh.indexCount == 0 ? render::DrawKind::Direct
                                           : render::DrawKind::Indexed;
    plan.batch.material = material.id.value;

    for (const auto semantic : assets::collect_required_attributes(material)) {
        auto located = find_attribute(mesh, semantic);
        if (!located.has_value()) {
            return failure(core::StatusCode::invalid_argument,
                           "AssetRender: mesh is missing a required attribute");
        }
        auto status = add_attribute_plan(plan, *located, options);
        if (!status.ok()) {
            return status;
        }
    }

    if (options.includeOptionalMappedAttributes) {
        for (const auto& stream : mesh.streams) {
            for (const auto& attribute : stream.attributes) {
                if (has_planned_semantic(plan.attributes,
                                         attribute.semantic) ||
                    !should_plan_optional_attribute(attribute, options)) {
                    continue;
                }
                auto located = find_attribute(mesh, attribute.semantic);
                if (!located.has_value()) {
                    continue;
                }
                auto status = add_attribute_plan(plan, *located, options);
                if (!status.ok()) {
                    return status;
                }
            }
        }
    }

    auto instanceCount = infer_instance_count(plan);
    if (!instanceCount.ok()) {
        return instanceCount.status();
    }
    plan.batch.instanceCount = instanceCount.value();
    plan.batch.variantHash =
        blend_variant(plan.batch.layout.hash(),
                      static_cast<std::size_t>(material.id.value));
    return plan;
}

core::Result<RenderBatchPlan> plan_catalog_render_batch(
    const assets::AssetCatalog& catalog,
    assets::AssetId mesh,
    const RenderBatchPlanOptions& options) {
    const auto* foundMesh = catalog.mesh(mesh);
    if (foundMesh == nullptr) {
        return failure(core::StatusCode::unavailable,
                       "AssetRender: mesh asset is not registered");
    }
    if (!foundMesh->material.valid()) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: mesh must reference a valid material");
    }
    const auto* foundMaterial = catalog.material(foundMesh->material);
    if (foundMaterial == nullptr) {
        return failure(core::StatusCode::unavailable,
                       "AssetRender: material asset is not registered");
    }
    return plan_render_batch(*foundMesh, *foundMaterial, options);
}

core::Result<AssetGroupRenderPlan> plan_group_render_batches(
    const assets::AssetCatalog& catalog,
    assets::AssetId group,
    const RenderBatchPlanOptions& options) {
    const auto* foundGroup = catalog.group(group);
    if (foundGroup == nullptr) {
        return failure(core::StatusCode::unavailable,
                       "AssetRender: asset group is not registered");
    }

    const auto report = catalog.validate_group(group);
    if (!report.ok()) {
        return failure(core::StatusCode::invalid_argument,
                       "AssetRender: asset group is not valid");
    }

    AssetGroupRenderPlan result;
    result.group = foundGroup->id;
    result.batches.reserve(foundGroup->meshes.size());
    for (const auto meshId : foundGroup->meshes) {
        auto planResult = plan_catalog_render_batch(catalog, meshId, options);
        if (!planResult.ok()) {
            return planResult.status();
        }
        result.batches.push_back(std::move(planResult).value());
    }
    return result;
}

} // namespace truffle::asset_render


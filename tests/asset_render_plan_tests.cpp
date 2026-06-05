#include "test_support.hpp"
#include "truffle/asset_render/asset_render.hpp"

namespace {

truffle::assets::GeometryStreamDesc make_detection_stream() {
    using namespace truffle::assets;

    GeometryStreamDesc stream;
    stream.id = AssetId{100};
    stream.name = "dense-detections";
    stream.role = GeometryStreamRole::Instance;
    stream.residency = DataResidency::External;
    stream.elementCount = 1'000'000;
    stream.byteSize = 32'000'000;
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Position,
        .format = AttributeFormat::Float32x3,
        .stream = 0,
        .offset = 0,
        .stride = 32,
        .name = "position",
    });
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Confidence,
        .format = AttributeFormat::Float32,
        .stream = 0,
        .offset = 12,
        .stride = 32,
        .name = "confidence",
    });
    stream.attributes.push_back({
        .semantic = AttributeSemantic::Classification,
        .format = AttributeFormat::UInt32,
        .stream = 0,
        .offset = 16,
        .stride = 32,
        .name = "classification",
    });
    return stream;
}

truffle::assets::MaterialAssetDesc make_detection_material() {
    using namespace truffle::assets;

    MaterialAssetDesc material;
    material.id = AssetId{200};
    material.name = "detection-material";
    material.requiredAttributes = {
        AttributeSemantic::Position,
        AttributeSemantic::Confidence,
        AttributeSemantic::Classification,
    };
    return material;
}

truffle::assets::MeshAssetDesc make_detection_mesh(
    const truffle::assets::GeometryStreamDesc& stream,
    truffle::assets::AssetId material) {
    using namespace truffle::assets;

    MeshAssetDesc mesh;
    mesh.id = AssetId{300};
    mesh.name = "detection-points";
    mesh.material = material;
    mesh.vertexCount = 1;
    mesh.streams.push_back(stream);
    return mesh;
}

truffle::asset_render::RenderBatchPlanOptions make_detection_options() {
    using namespace truffle;

    asset_render::RenderBatchPlanOptions options;
    options.bindingModel = render::BindingModel::Interleaved;
    options.channelMappings = {
        {assets::AttributeSemantic::Position,
         render::ChannelKind::CustomFloat4},
        {assets::AttributeSemantic::Confidence,
         render::ChannelKind::CustomFloat4},
        {assets::AttributeSemantic::Classification,
         render::ChannelKind::CustomFloat4},
    };
    return options;
}

} // namespace

int main() {
    using namespace truffle;

    const auto stream = make_detection_stream();
    const auto material = make_detection_material();
    const auto mesh = make_detection_mesh(stream, material.id);
    const auto options = make_detection_options();

    const auto unsupported =
        asset_render::plan_render_batch(mesh, material);
    TRUFFLE_CHECK(!unsupported.ok());
    TRUFFLE_CHECK(unsupported.status().code ==
                  core::StatusCode::unsupported);

    auto planResult =
        asset_render::plan_render_batch(mesh, material, options);
    TRUFFLE_CHECK(planResult.ok());
    const auto& plan = planResult.value();
    TRUFFLE_CHECK(plan.mesh == mesh.id);
    TRUFFLE_CHECK(plan.material == material.id);
    TRUFFLE_CHECK(plan.batch.layout.bindingModel ==
                  render::BindingModel::Interleaved);
    TRUFFLE_CHECK(plan.batch.layout.channels.size() == 3);
    TRUFFLE_CHECK(plan.attributes.size() == 3);
    TRUFFLE_CHECK(plan.bindings.size() == 1);
    TRUFFLE_CHECK(plan.bindings.front().binding == 0);
    TRUFFLE_CHECK(plan.bindings.front().stream == stream.id);
    TRUFFLE_CHECK(plan.bindings.front().elementCount == stream.elementCount);
    TRUFFLE_CHECK(plan.bindings.front().byteSize == stream.byteSize);

    const auto& position = plan.batch.layout.channels[0];
    const auto& confidence = plan.batch.layout.channels[1];
    const auto& classification = plan.batch.layout.channels[2];
    TRUFFLE_CHECK(position.kind == render::ChannelKind::CustomFloat4);
    TRUFFLE_CHECK(position.binding == 0);
    TRUFFLE_CHECK(position.offset == 0);
    TRUFFLE_CHECK(position.stride == 32);
    TRUFFLE_CHECK(confidence.kind == render::ChannelKind::CustomFloat4);
    TRUFFLE_CHECK(confidence.binding == 0);
    TRUFFLE_CHECK(confidence.offset == 12);
    TRUFFLE_CHECK(confidence.stride == 32);
    TRUFFLE_CHECK(classification.kind == render::ChannelKind::CustomFloat4);
    TRUFFLE_CHECK(classification.binding == 0);
    TRUFFLE_CHECK(classification.offset == 16);
    TRUFFLE_CHECK(classification.stride == 32);
    TRUFFLE_CHECK(plan.attributes[0].attribute.semantic ==
                  assets::AttributeSemantic::Position);
    TRUFFLE_CHECK(plan.attributes[1].attribute.semantic ==
                  assets::AttributeSemantic::Confidence);
    TRUFFLE_CHECK(plan.attributes[2].attribute.semantic ==
                  assets::AttributeSemantic::Classification);
    TRUFFLE_CHECK(plan.batch.instanceCount == stream.elementCount);
    TRUFFLE_CHECK(plan.batch.vertexCount == 1);
    TRUFFLE_CHECK(plan.batch.material == material.id.value);
    TRUFFLE_CHECK(plan.batch.kind == render::DrawKind::Direct);
    TRUFFLE_CHECK(plan.batch.bindings[0].buffer == nullptr);
    TRUFFLE_CHECK(plan.batch.variantHash != 0);

    assets::GeometryStreamDesc transformStream;
    transformStream.id = assets::AssetId{400};
    transformStream.name = "instance-transform";
    transformStream.role = assets::GeometryStreamRole::Instance;
    transformStream.elementCount = 4;
    transformStream.byteSize = 320;
    transformStream.attributes.push_back({
        .semantic = assets::AttributeSemantic::Transform,
        .format = assets::AttributeFormat::Float32x4x4,
        .stream = 0,
        .offset = 0,
        .stride = 80,
        .name = "transform",
    });
    transformStream.attributes.push_back({
        .semantic = assets::AttributeSemantic::Color,
        .format = assets::AttributeFormat::Float32x4,
        .stream = 0,
        .offset = 64,
        .stride = 80,
        .name = "color",
    });

    assets::MaterialAssetDesc transformMaterial;
    transformMaterial.id = assets::AssetId{401};
    transformMaterial.name = "transform-color";
    transformMaterial.requiredAttributes = {
        assets::AttributeSemantic::Transform,
        assets::AttributeSemantic::Color,
    };

    assets::MeshAssetDesc transformMesh;
    transformMesh.id = assets::AssetId{402};
    transformMesh.name = "instanced-quad";
    transformMesh.material = transformMaterial.id;
    transformMesh.vertexCount = 6;
    transformMesh.streams.push_back(transformStream);

    const auto defaultPlan =
        asset_render::plan_render_batch(transformMesh, transformMaterial);
    TRUFFLE_CHECK(defaultPlan.ok());
    TRUFFLE_CHECK(defaultPlan.value().batch.layout.channels.size() == 2);
    TRUFFLE_CHECK(defaultPlan.value().batch.layout.channels[0].kind ==
                  render::ChannelKind::Transform);
    TRUFFLE_CHECK(defaultPlan.value().batch.layout.channels[1].kind ==
                  render::ChannelKind::Color);
    TRUFFLE_CHECK(defaultPlan.value().batch.instanceCount == 4);

    assets::AssetCatalog catalog;
    TRUFFLE_CHECK(catalog.add_geometry_stream(stream).ok());
    TRUFFLE_CHECK(catalog.add_material(material).ok());
    TRUFFLE_CHECK(catalog.add_mesh(mesh).ok());

    assets::AssetGroupDesc group;
    group.id = assets::AssetId{500};
    group.name = "dense-group";
    group.tags = {"dense", "lidar"};
    group.geometryStreams.push_back(stream.id);
    group.materials.push_back(material.id);
    group.meshes.push_back(mesh.id);
    TRUFFLE_CHECK(catalog.add_group(group).ok());

    auto catalogPlan =
        asset_render::plan_catalog_render_batch(catalog, mesh.id, options);
    TRUFFLE_CHECK(catalogPlan.ok());
    TRUFFLE_CHECK(catalogPlan.value().batch.instanceCount ==
                  stream.elementCount);

    const auto missingCatalogPlan =
        asset_render::plan_catalog_render_batch(catalog,
                                                assets::AssetId{999},
                                                options);
    TRUFFLE_CHECK(!missingCatalogPlan.ok());
    TRUFFLE_CHECK(missingCatalogPlan.status().code ==
                  core::StatusCode::unavailable);

    auto groupPlan =
        asset_render::plan_group_render_batches(catalog, group.id, options);
    TRUFFLE_CHECK(groupPlan.ok());
    TRUFFLE_CHECK(groupPlan.value().group == group.id);
    TRUFFLE_CHECK(groupPlan.value().batches.size() == 1);
    TRUFFLE_CHECK(groupPlan.value().batches.front().batch.instanceCount ==
                  stream.elementCount);

    const auto missingGroupPlan =
        asset_render::plan_group_render_batches(catalog,
                                                assets::AssetId{998},
                                                options);
    TRUFFLE_CHECK(!missingGroupPlan.ok());
    TRUFFLE_CHECK(missingGroupPlan.status().code ==
                  core::StatusCode::unavailable);

    return 0;
}


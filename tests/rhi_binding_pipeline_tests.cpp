#include "rhi_test_utils.hpp"
#include "truffle/rhi/shader_package.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

truffle::rhi::Shader make_shader(
    truffle::rhi::Device& device, truffle::rhi::ShaderStage stage,
    std::vector<truffle::rhi::ResourceBinding> bindings,
    std::vector<truffle::rhi::PushConstantRange> pushConstants = {},
    std::vector<truffle::rhi::ShaderSpecializationConstant> constants = {},
    truffle::rhi::Extent3D requiredWorkgroupSize = {1, 1, 1},
    truffle::rhi::Extent3D preferredWorkgroupSize = {1, 1, 1}) {
    truffle::rhi::ShaderDesc desc;
    desc.stage = stage;
    desc.code = {std::byte{0x52}, std::byte{0x48}, std::byte{0x49}};
    desc.reflection = std::move(bindings);
    desc.pushConstants = std::move(pushConstants);
    desc.specializationConstants = std::move(constants);
    desc.requiredWorkgroupSize = requiredWorkgroupSize;
    desc.preferredWorkgroupSize = preferredWorkgroupSize;
    auto result = device.create_shader(desc);
    assert(result.ok());
    return std::move(result).value();
}

truffle::rhi::Texture make_texture(truffle::rhi::Device& device,
                                   truffle::rhi::TextureFormat format,
                                   truffle::rhi::TextureUsage usage,
                                   std::uint32_t samples = 1) {
    auto result = device.create_texture({
        .extent = {16, 16, 1},
        .format = format,
        .usage = usage,
        .sampleCount = samples,
    });
    assert(result.ok());
    return std::move(result).value();
}

void workgroup_contracts() {
    using namespace truffle;
    auto context = tests::make_null_context();
    const auto& limits = context.adapter.info().pipelines;
    rhi::ShaderDesc desc{
        .stage = rhi::ShaderStage::compute,
        .code = {std::byte{1}},
        .specializationConstants = {{7, "shared", rhi::ShaderValueType::uint32, 2},
                                    {0, "depth", rhi::ShaderValueType::sint32, 3}},
        .requiredWorkgroupSize = {2, 2, 3},
        .preferredWorkgroupSize = {2, 2, 3},
        .workgroupSpecializationIds = {7, 7, 0}};
    rhi::ShaderReflection reflection{
        .specializationConstants = desc.specializationConstants,
        .requiredWorkgroupSize = desc.requiredWorkgroupSize,
        .preferredWorkgroupSize = desc.preferredWorkgroupSize,
        .workgroupSpecializationIds = desc.workgroupSpecializationIds};
    rhi::ShaderPackageDesc packageDesc{
        .name = "workgroup",
        .sources = {{.path = "fixture", .sha256 = std::string(64, 'a')}},
        .compilers = {{.name = "fixture", .version = "1"}},
        .variants = {{.stage = rhi::ShaderStage::compute, .reflection = reflection, .code = desc.code}}};
    auto package = rhi::ShaderPackage::create(packageDesc);
    assert(package.ok());
    auto loaded = rhi::ShaderPackage::load(package.value().bytes());
    assert(loaded.ok());
    const auto& roundtrip = loaded.value().desc().variants[0].reflection;
    assert(roundtrip.workgroupSpecializationIds == desc.workgroupSpecializationIds);
    assert(roundtrip.requiredWorkgroupSize == desc.requiredWorkgroupSize);
    assert(roundtrip.specializationConstants.size() == 2);
    auto shaderResult = context.device.create_shader(loaded.value(), rhi::ShaderTarget::spirv,
                                                     "main", rhi::ShaderStage::compute);
    assert(shaderResult.ok());
    auto shader = std::move(shaderResult).value();
    auto defaults = context.device.create_compute_pipeline({.computeShader = &shader});
    assert(defaults.ok() && defaults.value().preferred_workgroup_size() == rhi::Extent3D(2, 2, 3));
    const std::vector<rhi::SpecializationValue> changed{{7, rhi::ShaderValueType::uint32, 5},
                                                      {0, rhi::ShaderValueType::sint32, 4}};
    auto specialized = context.device.create_compute_pipeline({
        .computeShader = &shader, .requiredWorkgroupSize = {5, 5, 4},
        .specializationConstants = changed});
    assert(specialized.ok() && specialized.value().preferred_workgroup_size() == rhi::Extent3D(5, 5, 4));
    auto explicitPreferred = context.device.create_compute_pipeline({
        .computeShader = &shader, .preferredWorkgroupSize = {1, 1, 1}, .specializationConstants = changed});
    assert(explicitPreferred.ok() && explicitPreferred.value().preferred_workgroup_size() == rhi::Extent3D(1, 1, 1));
    assert(!context.device.create_compute_pipeline({
        .computeShader = &shader, .requiredWorkgroupSize = {2, 2, 3},
        .specializationConstants = changed}).ok());
    assert(!context.device.create_compute_pipeline({
        .computeShader = &shader, .requiredWorkgroupSize = {0, 2, 3}}).ok());
    for (const auto& values : std::vector<std::vector<rhi::SpecializationValue>>{
            {{7, rhi::ShaderValueType::uint32, 0}},
            {{0, rhi::ShaderValueType::sint32, 0xffffffffu}},
            {{0, rhi::ShaderValueType::sint32, 0}},
            {{0, rhi::ShaderValueType::uint32, 4}},
            {{7, rhi::ShaderValueType::float32, 4}},
            {{99, rhi::ShaderValueType::uint32, 4}},
            {{7, rhi::ShaderValueType::uint32, 4}, {7, rhi::ShaderValueType::uint32, 4}}}) {
        auto invalid = context.device.create_compute_pipeline({
            .computeShader = &shader, .specializationConstants = values});
        assert(!invalid.ok() && invalid.status().code == rhi::StatusCode::invalid_argument);
    }
    const auto rejects = [&](rhi::ShaderDesc invalid) {
        assert(!context.device.create_shader(invalid).ok());
        auto malformed = packageDesc;
        auto& r = malformed.variants[0].reflection;
        r.specializationConstants = invalid.specializationConstants;
        r.requiredWorkgroupSize = invalid.requiredWorkgroupSize;
        r.workgroupSpecializationIds = invalid.workgroupSpecializationIds;
        assert(!rhi::ShaderPackage::create(malformed).ok());
    };
    auto invalid = desc;
    invalid.workgroupSpecializationIds[0] = 99;
    rejects(invalid);
    invalid = desc;
    invalid.requiredWorkgroupSize.height = 3;
    rejects(invalid);
    for (const auto type : {rhi::ShaderValueType::float32, rhi::ShaderValueType::boolean}) {
        invalid = desc;
        invalid.specializationConstants[0].type = type;
        rejects(invalid);
    }
    invalid = desc;
    invalid.specializationConstants[1].defaultValueBits = 0xffffffffu;
    invalid.requiredWorkgroupSize.depth = 0xffffffffu;
    rejects(invalid);
    invalid = desc;
    invalid.specializationConstants[0].defaultValueBits = 0;
    invalid.requiredWorkgroupSize.width = invalid.requiredWorkgroupSize.height = 0;
    rejects(invalid);
    invalid = desc;
    invalid.stage = rhi::ShaderStage::vertex;
    assert(!context.device.create_shader(invalid).ok());
    const auto axisLimit = context.device.create_compute_pipeline({
        .computeShader = &shader,
        .specializationConstants = {{7, rhi::ShaderValueType::uint32, limits.maxComputeWorkgroupSize.width + 1}}});
    assert(!axisLimit.ok() && axisLimit.status().code == rhi::StatusCode::unsupported);
    const auto side = std::min(limits.maxComputeWorkgroupSize.width, limits.maxComputeWorkgroupSize.height);
    assert(static_cast<std::uint64_t>(side) * side * 3 > limits.maxComputeInvocations);
    const auto totalLimit = context.device.create_compute_pipeline({
        .computeShader = &shader, .specializationConstants = {{7, rhi::ShaderValueType::uint32, side}}});
    assert(!totalLimit.ok() && totalLimit.status().code == rhi::StatusCode::unsupported);
    invalid = desc;
    invalid.specializationConstants.clear();
    invalid.workgroupSpecializationIds = {};
    invalid.requiredWorkgroupSize = {0xffffffffu, 0xffffffffu, 0xffffffffu};
    auto hugeShader = context.device.create_shader(invalid);
    assert(hugeShader.ok());
    assert(!context.device.create_compute_pipeline({.computeShader = &hugeShader.value()}).ok());
    // Out-of-limit defaults can be specialized into a valid extent before limits.
    invalid = desc;
    invalid.specializationConstants[0].defaultValueBits = side;
    invalid.requiredWorkgroupSize = {side, side, 3};
    invalid.preferredWorkgroupSize = invalid.requiredWorkgroupSize;
    auto largeDefault = context.device.create_shader(invalid);
    assert(largeDefault.ok());
    assert(!context.device.create_compute_pipeline({.computeShader = &largeDefault.value()}).ok());
    auto reduced = context.device.create_compute_pipeline({
        .computeShader = &largeDefault.value(),
        .specializationConstants = {{7, rhi::ShaderValueType::uint32, 2}}});
    assert(reduced.ok() && reduced.value().preferred_workgroup_size() == rhi::Extent3D(2, 2, 3));
    auto crossTarget = packageDesc;
    auto other = crossTarget.variants[0];
    other.target = rhi::ShaderTarget::dxil;
    other.format = rhi::ShaderByteFormat::dxil;
    crossTarget.variants.push_back(other);
    assert(rhi::ShaderPackage::create(crossTarget).ok());
    crossTarget.variants[1].reflection.workgroupSpecializationIds[0] = std::nullopt;
    assert(!rhi::ShaderPackage::create(crossTarget).ok());
}

void directional_interfaces() {
    using namespace truffle;
    auto context = tests::make_null_context();
    assert(context.device.create_pipeline({}).ok());
    assert(context.device.create_shader({
        .stage = rhi::ShaderStage::vertex, .code = {std::byte{1}},
        .requiredWorkgroupSize = {0, 0, 0}}).ok());
    rhi::ShaderPackageDesc desc{
        .name = "directional",
        .sources = {{.path = "fixture", .sha256 = std::string(64, 'a')}},
        .compilers = {{.name = "fixture", .version = "1"}},
        .variants = {
            {.stage = rhi::ShaderStage::vertex,
             .reflection = {.interfaces = {
                 {rhi::ShaderStage::vertex, rhi::ShaderInterfaceDirection::input, 0, "position", "float3", ""},
                 {rhi::ShaderStage::vertex, rhi::ShaderInterfaceDirection::output, 0, "varying", "float4", ""}}},
             .code = {std::byte{1}}},
            {.stage = rhi::ShaderStage::fragment,
             .reflection = {.interfaces = {
                 {rhi::ShaderStage::fragment, rhi::ShaderInterfaceDirection::input, 0, "renamed", "vec4", "smooth"},
                 {rhi::ShaderStage::fragment, rhi::ShaderInterfaceDirection::output, 0, "color", "vec4", ""}}},
             .code = {std::byte{1}}}}};
    auto package = rhi::ShaderPackage::create(desc);
    assert(package.ok());
    auto loaded = rhi::ShaderPackage::load(package.value().bytes());
    assert(loaded.ok());
    for (const auto& variant : loaded.value().desc().variants) {
        assert(variant.reflection.interfaces.size() == 2);
        assert(variant.reflection.interfaces[0].direction == rhi::ShaderInterfaceDirection::input);
        assert(variant.reflection.interfaces[1].direction == rhi::ShaderInterfaceDirection::output);
        assert(variant.reflection.interfaces[0].location == variant.reflection.interfaces[1].location);
    }
    auto vertex = context.device.create_shader(loaded.value(), rhi::ShaderTarget::spirv, "main", rhi::ShaderStage::vertex);
    auto fragment = context.device.create_shader(loaded.value(), rhi::ShaderTarget::spirv, "main", rhi::ShaderStage::fragment);
    assert(vertex.ok() && fragment.ok());
    assert(!context.device.create_pipeline({.fragmentShader = &fragment.value()}).ok());
    rhi::PipelineDesc pipeline{
        .vertexShader = &vertex.value(), .fragmentShader = &fragment.value(),
        .vertexBuffers = {{.stride = 12, .attributes = {{.location = 0, .format = rhi::VertexFormat::float32x3}}}}};
    assert(context.device.create_pipeline(pipeline).ok());
    auto depthOnly = pipeline;
    depthOnly.fragmentShader = nullptr;
    assert(context.device.create_pipeline(depthOnly).ok());
    pipeline.vertexBuffers.clear();
    assert(!context.device.create_pipeline(pipeline).ok());
    pipeline.vertexBuffers = {{.stride = 12, .attributes = {{.location = 0, .format = rhi::VertexFormat::uint32x3}}}};
    assert(!context.device.create_pipeline(pipeline).ok());
    pipeline.vertexBuffers[0].attributes[0].format = rhi::VertexFormat::float32x3;
    for (const bool wrongType : {false, true}) {
        auto mismatch = desc;
        auto& input = mismatch.variants[1].reflection.interfaces[0];
        if (wrongType) input.type = "vec3";
        else input.location = 3;
        auto mismatchedPackage = rhi::ShaderPackage::create(mismatch);
        assert(mismatchedPackage.ok());
        auto shader = context.device.create_shader(mismatchedPackage.value(), rhi::ShaderTarget::spirv,
                                                   "main", rhi::ShaderStage::fragment);
        assert(shader.ok());
        pipeline.fragmentShader = &shader.value();
        assert(!context.device.create_pipeline(pipeline).ok());
    }
}

} // namespace

int main() {
    using namespace truffle;
    auto context = tests::make_null_context();
    const auto& capabilities = context.adapter.info();
    assert(capabilities.bindings.ordinaryBindGroups);
    assert(capabilities.bindings.descriptorArrays);
    assert(capabilities.bindings.dynamicOffsets);
    assert(capabilities.bindings.immutableSamplers);
    assert(capabilities.bindings.pushConstants);
    assert(!capabilities.bindings.bindlessTables);
    assert(!capabilities.bindings.updateAfterBind);
    assert(capabilities.pipelines.graphics);
    assert(capabilities.pipelines.compute);
    assert(capabilities.pipelines.multipleRenderTargets);
    assert(capabilities.pipelines.depthStencil);
    assert(capabilities.pipelines.multisample);
    assert(capabilities.pipelines.indirect);
    assert(capabilities.pipelines.indirectCount);
    assert(capabilities.pipelines.pipelineCache);

    auto samplerResult = context.device.create_sampler({
        .minFilter = rhi::Filter::linear,
        .magFilter = rhi::Filter::linear,
        .addressU = rhi::SamplerAddressMode::repeat,
        .debugName = "immutable sampler",
    });
    assert(samplerResult.ok());
    auto sampler = std::move(samplerResult).value();

    auto layoutResult = context.device.create_bind_group_layout({
        .group = 0,
        .entries = {
            {.binding = 0,
             .type = rhi::BindingType::uniform_buffer,
             .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::vertex,
             .dynamicOffset = true,
             .minimumBufferSize = 16},
            {.binding = 1,
             .type = rhi::BindingType::sampled_texture,
             .arrayCount = 2,
             .visibility = rhi::ShaderStageMask::fragment},
            {.binding = 2,
             .type = rhi::BindingType::sampler,
             .visibility = rhi::ShaderStageMask::fragment,
             .immutableSampler = &sampler},
        },
        .debugName = "material layout",
    });
    assert(layoutResult.ok());
    auto layout = std::move(layoutResult).value();

    auto arenaResult = context.device.create_descriptor_arena({
        .maxBindGroups = 2,
        .maxDescriptors = 16,
        .debugName = "frame descriptors",
    });
    assert(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    const auto initialEpoch = arena.epoch();

    auto uniform0 = tests::make_buffer(context.device, 512,
                                       rhi::BufferUsage::uniform);
    auto uniform1 = tests::make_buffer(context.device, 512,
                                       rhi::BufferUsage::uniform);
    auto sampled0 = make_texture(context.device, rhi::TextureFormat::rgba8_unorm,
                                 rhi::TextureUsage::sampled);
    auto sampled1 = make_texture(context.device, rhi::TextureFormat::rgba8_unorm,
                                 rhi::TextureUsage::sampled);
    auto view0Result = context.device.create_texture_view(sampled0);
    auto view1Result = context.device.create_texture_view(sampled1);
    assert(view0Result.ok() && view1Result.ok());
    auto view0 = std::move(view0Result).value();
    auto view1 = std::move(view1Result).value();

    rhi::BindGroupDesc groupDesc{
        .layout = &layout,
        .arena = &arena,
        .entries = {
            {.binding = 0,
             .arrayElement = 0,
             .buffer = &uniform0,
             .size = 16},
            {.binding = 0,
             .arrayElement = 1,
             .buffer = &uniform1,
             .size = 16},
            {.binding = 1, .arrayElement = 0, .textureView = &view0},
            {.binding = 1, .arrayElement = 1, .textureView = &view1},
        },
        .debugName = "material group",
    };
    auto incompleteDesc = groupDesc;
    incompleteDesc.entries.pop_back();
    const auto incomplete = context.device.create_bind_group(incompleteDesc);
    assert(!incomplete.ok());
    assert(incomplete.status().code == rhi::StatusCode::invalid_argument);

    auto groupResult = context.device.create_bind_group(groupDesc);
    assert(groupResult.ok());
    auto group = std::move(groupResult).value();
    assert(group.layout_id() == layout.id());

    auto pipelineLayoutResult = context.device.create_pipeline_layout({
        .bindGroupLayouts = {&layout},
        .pushConstants = {
            {.stage = rhi::ShaderStage::vertex, .offset = 0, .size = 16},
            {.stage = rhi::ShaderStage::fragment, .offset = 0, .size = 16},
        },
    });
    assert(pipelineLayoutResult.ok());
    auto pipelineLayout = std::move(pipelineLayoutResult).value();

    auto vertex = make_shader(
        context.device, rhi::ShaderStage::vertex,
        {{.name = "camera",
          .stage = rhi::ShaderStage::vertex,
          .type = rhi::ResourceBindingType::buffer,
          .group = 0,
          .binding = 0,
          .arrayCount = 2,
          .minimumSize = 16}},
        {{.stage = rhi::ShaderStage::vertex, .offset = 0, .size = 16}},
        {{.id = 7,
          .name = "mode",
          .type = rhi::ShaderValueType::uint32,
          .defaultValueBits = 0}});
    auto fragment = make_shader(
        context.device, rhi::ShaderStage::fragment,
        {{.name = "images",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::texture,
          .group = 0,
          .binding = 1,
          .arrayCount = 2},
         {.name = "linearSampler",
          .stage = rhi::ShaderStage::fragment,
          .type = rhi::ResourceBindingType::sampler,
          .group = 0,
          .binding = 2}},
        {{.stage = rhi::ShaderStage::fragment, .offset = 0, .size = 16}});

    auto graphicsResult = context.device.create_pipeline({
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .layout = &pipelineLayout,
        .dynamicState = rhi::DynamicState::viewport |
                        rhi::DynamicState::scissor |
                        rhi::DynamicState::blend_constant |
                        rhi::DynamicState::stencil_reference |
                        rhi::DynamicState::depth_bias,
        .specializationConstants = {
            {.id = 7, .type = rhi::ShaderValueType::uint32, .valueBits = 2},
        },
    });
    assert(graphicsResult.ok());
    auto graphics = std::move(graphicsResult).value();

    const auto badSpecialization = context.device.create_pipeline({
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .layout = &pipelineLayout,
        .specializationConstants = {
            {.id = 99, .type = rhi::ShaderValueType::uint32, .valueBits = 1},
        },
    });
    assert(!badSpecialization.ok());

    auto mismatchResult = context.device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::sampled_texture,
                     .arrayCount = 2,
                     .visibility = rhi::ShaderStageMask::vertex}},
    });
    assert(mismatchResult.ok());
    auto mismatch = std::move(mismatchResult).value();
    auto mismatchPipelineLayoutResult = context.device.create_pipeline_layout({
        .bindGroupLayouts = {&mismatch},
    });
    assert(mismatchPipelineLayoutResult.ok());
    auto mismatchPipelineLayout =
        std::move(mismatchPipelineLayoutResult).value();
    const auto mismatchedPipeline = context.device.create_pipeline({
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .layout = &mismatchPipelineLayout,
    });
    assert(!mismatchedPipeline.ok());
    assert(mismatchedPipeline.status().code == rhi::StatusCode::invalid_argument);

    const auto bindless = context.device.create_bindless_table({
        .layout = &layout,
        .capacity = 1024,
    });
    assert(!bindless.ok());
    assert(bindless.status().code == rhi::StatusCode::unsupported);
    const auto updateAfterBind = context.device.create_descriptor_arena({
        .updateAfterBind = true,
    });
    assert(!updateAfterBind.ok());
    assert(updateAfterBind.status().code == rhi::StatusCode::unsupported);

    auto cacheResult = context.device.create_pipeline_cache({
        .initialData = {std::byte{1}, std::byte{3}, std::byte{3}, std::byte{7}},
    });
    assert(cacheResult.ok());
    auto cache = std::move(cacheResult).value();
    assert(cache.data().ok());
    assert(cache.data().value() ==
           std::vector<std::byte>({std::byte{1}, std::byte{3}, std::byte{3},
                                   std::byte{7}}));

    auto poolResult = context.device.create_command_pool(rhi::QueueKind::graphics);
    assert(poolResult.ok());
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    assert(listResult.ok());
    auto list = std::move(listResult).value();
    assert(list.begin().ok());
    auto renderResult = list.begin_rendering({.extent = {16, 16}});
    assert(renderResult.ok());
    auto render = std::move(renderResult).value();
    assert(!render.bind_group(0, group, std::array<std::uint32_t, 2>{256, 256})
                .ok());
    assert(render.bind_pipeline(graphics).ok());
    assert(!render.bind_group(1, group, std::array<std::uint32_t, 2>{256, 256})
                .ok());
    assert(!render.bind_group(0, group, std::array<std::uint32_t, 1>{256}).ok());
    assert(!render.bind_group(0, group, std::array<std::uint32_t, 2>{4, 256})
                .ok());
    assert(render.bind_group(0, group,
                             std::array<std::uint32_t, 2>{256, 256})
               .ok());
    const std::array<std::byte, 16> pushData{};
    assert(render
               .push_constants(rhi::ShaderStageMask::vertex |
                                   rhi::ShaderStageMask::fragment,
                               0, pushData)
               .ok());
    assert(!render.draw(3, 2, 4, 6).ok());
    assert(render.set_viewports(0, std::array<rhi::Viewport, 1>{
                                       rhi::Viewport{0, 0, 16, 16, 0, 1}})
               .ok());
    assert(render.set_scissors(
                     0, std::array<rhi::ScissorRect, 1>{{0, 0, 16, 16}})
               .ok());
    assert(render.set_blend_constant({0.25F, 0.5F, 0.75F, 1.0F}).ok());
    assert(render.set_stencil_reference(3).ok());
    assert(render.set_depth_bias(1.0F, 2.0F, 0.0F).ok());
    assert(render.draw(3, 2, 4, 6).ok());
    auto indexBuffer = tests::make_buffer(context.device, 64,
                                          rhi::BufferUsage::index);
    assert(render.bind_index_buffer(indexBuffer, 0, rhi::IndexFormat::uint16)
               .ok());
    assert(render.draw_indexed(6, 3, 2, -1, 5).ok());
    auto indirect = tests::make_buffer(context.device, 64,
                                       rhi::BufferUsage::indirect);
    auto indirectCount = tests::make_buffer(context.device, 4,
                                            rhi::BufferUsage::indirect);
    assert(render.draw_indirect(indirect, 0, false, 2, 16).ok());
    assert(render
               .draw_indirect_count(indirect, 0, indirectCount, 0, 2, 16,
                                    false)
               .ok());
    assert(render.end().ok());
    assert(list.end().ok());
    auto queueResult = context.device.queue(rhi::QueueKind::graphics);
    assert(queueResult.ok());
    auto queue = std::move(queueResult).value();
    std::array<rhi::CommandList*, 1> submitted{&list};
    assert(queue.submit(submitted).ok());

    assert(arena.reset().ok());
    assert(arena.epoch() != initialEpoch);
    assert(!group.valid());

    auto storageLayoutResult = context.device.create_bind_group_layout({
        .group = 1,
        .entries = {{.binding = 0,
                     .type = rhi::BindingType::storage_buffer,
                     .visibility = rhi::ShaderStageMask::compute,
                     .minimumBufferSize = 16}},
    });
    assert(storageLayoutResult.ok());
    auto storageLayout = std::move(storageLayoutResult).value();
    auto computeLayoutResult = context.device.create_pipeline_layout({
        .bindGroupLayouts = {&storageLayout},
        .pushConstants = {
            {.stage = rhi::ShaderStage::compute, .offset = 0, .size = 16},
        },
    });
    assert(computeLayoutResult.ok());
    auto computeLayout = std::move(computeLayoutResult).value();
    auto computeShader = make_shader(
        context.device, rhi::ShaderStage::compute,
        {{.name = "output",
          .stage = rhi::ShaderStage::compute,
          .type = rhi::ResourceBindingType::buffer,
          .group = 1,
          .binding = 0,
          .minimumSize = 16,
          .readOnly = false}},
        {{.stage = rhi::ShaderStage::compute, .offset = 0, .size = 16}}, {},
        {8, 1, 1}, {16, 1, 1});
    const auto badCompute = context.device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &computeLayout,
        .requiredWorkgroupSize = {4, 1, 1},
    });
    assert(!badCompute.ok());
    auto computeResult = context.device.create_compute_pipeline({
        .computeShader = &computeShader,
        .layout = &computeLayout,
        .cache = &cache,
    });
    assert(computeResult.ok());
    auto compute = std::move(computeResult).value();
    assert(compute.preferred_workgroup_size() == rhi::Extent3D(16, 1, 1));
    auto storage = tests::make_buffer(context.device, 64,
                                      rhi::BufferUsage::storage);
    rhi::BindGroupDesc storageGroupDesc{
        .layout = &storageLayout,
        .arena = &arena,
        .entries = {{.binding = 0, .buffer = &storage, .size = 64}},
    };
    auto storageGroupResult = context.device.create_bind_group(storageGroupDesc);
    assert(storageGroupResult.ok());
    auto storageGroup = std::move(storageGroupResult).value();
    auto computePoolResult =
        context.device.create_command_pool(rhi::QueueKind::compute);
    assert(computePoolResult.ok());
    auto computePool = std::move(computePoolResult).value();
    auto computeListResult = computePool.allocate();
    assert(computeListResult.ok());
    auto computeList = std::move(computeListResult).value();
    assert(computeList.begin().ok());
    auto computeEncoderResult = computeList.begin_compute();
    assert(computeEncoderResult.ok());
    auto computeEncoder = std::move(computeEncoderResult).value();
    assert(computeEncoder.bind_pipeline(compute).ok());
    assert(computeEncoder.bind_group(1, storageGroup).ok());
    assert(computeEncoder.push_constants(0, pushData).ok());
    assert(computeEncoder.dispatch(2, 3, 4).ok());
    auto dispatchArgs = tests::make_buffer(context.device, 12,
                                           rhi::BufferUsage::indirect);
    assert(computeEncoder.dispatch_indirect(dispatchArgs, 0).ok());
    assert(computeEncoder.end().ok());
    assert(computeList.end().ok());
    auto computeQueueResult = context.device.queue(rhi::QueueKind::compute);
    assert(computeQueueResult.ok());
    auto computeQueue = std::move(computeQueueResult).value();
    std::array<rhi::CommandList*, 1> computeSubmission{&computeList};
    assert(computeQueue.submit(computeSubmission).ok());

    auto color0 = make_texture(
        context.device, rhi::TextureFormat::rgba8_unorm,
        rhi::TextureUsage::color_attachment, 4);
    auto color1 = make_texture(
        context.device, rhi::TextureFormat::rgba16_float,
        rhi::TextureUsage::color_attachment, 4);
    auto resolve0 = make_texture(
        context.device, rhi::TextureFormat::rgba8_unorm,
        rhi::TextureUsage::color_attachment, 1);
    auto depth = make_texture(
        context.device, rhi::TextureFormat::depth32_float,
        rhi::TextureUsage::depth_stencil_attachment, 4);
    auto attachmentPipelineResult = context.device.create_pipeline({
        .vertexShader = &vertex,
        .fragmentShader = &fragment,
        .layout = &pipelineLayout,
        .colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm},
                         {.format = rhi::TextureFormat::rgba16_float}},
        .depthStencil = {.format = rhi::TextureFormat::depth32_float,
                         .depthWriteEnabled = true,
                         .depthCompare = rhi::CompareOp::less},
        .multisample = {.sampleCount = 4},
    });
    assert(attachmentPipelineResult.ok());
    auto attachmentPipeline = std::move(attachmentPipelineResult).value();
    assert(list.reset().ok());
    assert(list.begin().ok());
    auto attachmentRenderResult = list.begin_rendering({
        .extent = {16, 16},
        .colorAttachments = {{.texture = &color0, .resolveTexture = &resolve0},
                             {.texture = &color1}},
        .depthStencilAttachment = {.texture = &depth},
    });
    assert(attachmentRenderResult.ok());
    auto attachmentRender = std::move(attachmentRenderResult).value();
    assert(attachmentRender.bind_pipeline(attachmentPipeline).ok());
    assert(attachmentRender.draw(3).ok());
    assert(attachmentRender.end().ok());
    assert(list.end().ok());
    assert(queue.submit(submitted).ok());

    const auto stats = context.instance.stats();
    assert(stats.samplersCreated == 1);
    assert(stats.descriptorArenasCreated == 1);
    assert(stats.bindGroupsCreated == 2);
    assert(stats.pipelineCachesCreated == 1);
    assert(stats.drawsRecorded == 5);
    assert(stats.dispatchesRecorded == 2);
    workgroup_contracts();
    directional_interfaces();
    return 0;
}

#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/renderer/renderer.hpp"

#include <array>
#include <cassert>

namespace {

truffle::rhi::ShaderPackage make_package() {
    using namespace truffle::rhi;
    ShaderPackageDesc desc;
    desc.name = "prepared renderer program";
    desc.sources = {{.path = "fixture", .language = ShaderSourceLanguage::backend_native,
                     .sha256 = "df7e70e5021544f4834bbee64a9e3789febc4be81470df629cad6ddb03320a5c"}};
    desc.compilers = {{.name = "Null fixture", .version = "1"}};
    desc.permutations = {{.name = "configured"}};
    for (const auto stage : {ShaderStage::vertex, ShaderStage::fragment}) {
        desc.variants.push_back({
            .target = ShaderTarget::metal_source,
            .format = ShaderByteFormat::native_source,
            .kind = ShaderVariantKind::native_override,
            .stage = stage,
            .entryPoint = stage == ShaderStage::vertex ? "vertex_main" : "fragment_main",
            .permutation = "configured",
            .code = {std::byte{0x42}},
        });
    }
    auto result = ShaderPackage::create(std::move(desc));
    assert(result.ok());
    return std::move(result).value();
}

truffle::renderer::ProgramDesc program_desc(const truffle::rhi::ShaderPackage& package) {
    using namespace truffle;
    renderer::ProgramDesc desc;
    desc.vertex = {&package, rhi::ShaderTarget::metal_source, "vertex_main",
                   rhi::ShaderStage::vertex, "configured"};
    desc.fragment = {&package, rhi::ShaderTarget::metal_source, "fragment_main",
                     rhi::ShaderStage::fragment, "configured"};
    return desc;
}

} // namespace

int main() {
    using namespace truffle;
    auto context = tests::make_null_context();
    renderer::PipelineCache cache(context.device);
    auto missing = cache.get_or_create({}, 7);
    TRUFFLE_CHECK(!missing.ok());
    TRUFFLE_CHECK(missing.status().code == base::StatusCode::invalid_argument);

    {
        auto package = make_package();
        TRUFFLE_CHECK(cache.register_program(7, program_desc(package)).ok());
    }
    const auto first = cache.get_or_create({}, 7).value()->id();
    const auto created = context.instance.stats().pipelinesCreated;
    renderer::InstanceLayout metadata;
    metadata.channels.push_back({renderer::ChannelKind::Color, 3, 0, 16});
    TRUFFLE_CHECK(cache.get_or_create(metadata, 7).value()->id() == first);
    TRUFFLE_CHECK(context.instance.stats().pipelinesCreated == created);
    TRUFFLE_CHECK(!cache.get_or_create({}, 7, 42).ok());

    auto package = make_package();
    auto desc = program_desc(package);
    {
        auto vertexDesc = package.desc();
        std::erase_if(vertexDesc.variants, [](const auto& variant) {
            return variant.stage != rhi::ShaderStage::vertex;
        });
        auto fragmentDesc = package.desc();
        std::erase_if(fragmentDesc.variants, [](const auto& variant) {
            return variant.stage != rhi::ShaderStage::fragment;
        });
        auto vertexPackage = rhi::ShaderPackage::create(std::move(vertexDesc));
        auto fragmentPackage = rhi::ShaderPackage::create(std::move(fragmentDesc));
        TRUFFLE_CHECK(vertexPackage.ok() && fragmentPackage.ok());
        auto split = desc;
        split.vertex.package = &vertexPackage.value();
        split.fragment.package = &fragmentPackage.value();
        TRUFFLE_CHECK(cache.register_program(9, split).ok());
    }
    TRUFFLE_CHECK(cache.get_or_create({}, 9).ok());
    auto requiredDesc = package.desc();
    requiredDesc.requiredFeatures = {rhi::Feature::external_memory};
    auto requiredPackage = rhi::ShaderPackage::create(std::move(requiredDesc));
    TRUFFLE_CHECK(requiredPackage.ok());
    const auto unsupported = cache.register_program(7, program_desc(requiredPackage.value()));
    TRUFFLE_CHECK(unsupported.code == base::StatusCode::unsupported);
    auto pushDesc = package.desc();
    for (auto& variant : pushDesc.variants) {
        variant.reflection.pushConstants = {{.stage = variant.stage, .size = 16}};
    }
    auto pushPackage = rhi::ShaderPackage::create(std::move(pushDesc));
    TRUFFLE_CHECK(pushPackage.ok());
    TRUFFLE_CHECK(cache.register_program(7, program_desc(pushPackage.value())).code ==
                   base::StatusCode::unsupported);
    auto bad = desc;
    bad.fragment.package = nullptr;
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.fragment.target = rhi::ShaderTarget::spirv;
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.fragment.entryPoint = "missing";
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.vertex.permutation = "missing";
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.vertex.stage = rhi::ShaderStage::compute;
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.pipeline.colorTargets = {{.writeMask = 0x40}};
    TRUFFLE_CHECK(!cache.register_program(7, bad).ok());
    bad = desc;
    bad.pipeline.dynamicState = rhi::DynamicState::viewport;
    TRUFFLE_CHECK(cache.register_program(7, bad).code == base::StatusCode::unsupported);
    TRUFFLE_CHECK(cache.get_or_create({}, 7).value()->id() == first);

    auto candidate = cache.stage_program(desc);
    TRUFFLE_CHECK(candidate.ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 7).value()->id() == first);
    renderer::PipelineCache otherCache(context.device);
    TRUFFLE_CHECK(!otherCache.commit_program(7, std::move(candidate).value()).ok());
    TRUFFLE_CHECK(cache.commit_program(7, std::move(candidate).value()).ok());
    const auto second = cache.get_or_create({}, 7).value()->id();
    TRUFFLE_CHECK(second != first);
    TRUFFLE_CHECK(!cache.commit_program(7, std::move(candidate).value()).ok());
    TRUFFLE_CHECK(cache.register_program(7, desc, 42).ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 7, 42).value()->id() != second);

    auto wrongStages = renderer::ShaderBinding{
        tests::make_shader(context.device, rhi::ShaderStage::compute),
        tests::make_shader(context.device, rhi::ShaderStage::fragment)};
    TRUFFLE_CHECK(!cache.register_shaders(7, std::move(wrongStages), {}).ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 7).value()->id() == second);

    auto layoutResult = context.device.create_bind_group_layout({
        .group = 0,
        .entries = {{.binding = 0, .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = 16}},
    });
    TRUFFLE_CHECK(layoutResult.ok());
    auto layout = std::move(layoutResult).value();
    auto pipelineLayoutResult = context.device.create_pipeline_layout({
        .bindGroupLayouts = {&layout},
    });
    TRUFFLE_CHECK(pipelineLayoutResult.ok());
    auto pipelineLayout = std::move(pipelineLayoutResult).value();
    const auto shaders = [&](std::size_t minimumSize = 16) {
        return renderer::ShaderBinding{
            tests::make_shader(context.device, rhi::ShaderStage::vertex),
            tests::make_shader(context.device, rhi::ShaderStage::fragment,
                               {{.name = "color", .stage = rhi::ShaderStage::fragment,
                                 .binding = 0, .minimumSize = minimumSize}})};
    };
    // Missing reflection layout fails before changing the working material.
    TRUFFLE_CHECK(!cache.register_shaders(7, shaders(), {}).ok());
    // A different pipeline-layout identity is rejected at commit.
    auto changedLayout = cache.stage_shaders(shaders(), {.layout = &pipelineLayout});
    TRUFFLE_CHECK(changedLayout.ok());
    TRUFFLE_CHECK(!cache.commit_program(7, std::move(changedLayout).value()).ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 7).value()->id() == second);
    TRUFFLE_CHECK(cache.commit_program(8, std::move(changedLayout).value()).ok());
    // Both signatures fit the same layout, but publication must compare the
    // reflected binding contents, not just layout identity or binding count.
    const auto boundProgramId = cache.get_or_create({}, 8).value()->id();
    auto changedBindings = cache.stage_shaders(shaders(4), {.layout = &pipelineLayout});
    TRUFFLE_CHECK(changedBindings.ok());
    const auto bindingStatus =
        cache.commit_program(8, std::move(changedBindings).value());
    TRUFFLE_CHECK(bindingStatus.code == base::StatusCode::invalid_argument);
    TRUFFLE_CHECK(cache.get_or_create({}, 8).value()->id() == boundProgramId);

    auto arenaResult = context.device.create_descriptor_arena();
    TRUFFLE_CHECK(arenaResult.ok());
    auto arena = std::move(arenaResult).value();
    auto uniform = tests::make_buffer(context.device, 16, rhi::BufferUsage::uniform);
    auto groupResult = context.device.create_bind_group({
        .layout = &layout, .arena = &arena,
        .entries = {{.binding = 0, .buffer = &uniform, .size = 16}},
    });
    TRUFFLE_CHECK(groupResult.ok());
    auto group = std::move(groupResult).value();

    renderer::Renderer renderer(context.device, &cache);
    renderer::RenderBatch batch;
    batch.material = 8;
    batch.instanceCount = 1;
    const auto graph = [&]() {
        renderer::FrameGraph result;
        result.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{}, std::vector<renderer::RenderBatch>{batch}));
        return result;
    };
    TRUFFLE_CHECK(!renderer.render(graph()).ok());
    batch.bindGroups = {{.bindings = &group}};
    TRUFFLE_CHECK(renderer.render(graph()).ok());
    batch.bindGroups.push_back({.bindings = &group});
    TRUFFLE_CHECK(!renderer.render(graph()).ok());
    batch.bindGroups.clear();
    batch.material = 404;
    const auto status = renderer.render(graph());
    TRUFFLE_CHECK(status.code == base::StatusCode::invalid_argument);
    TRUFFLE_CHECK(status.message.find("not registered") != std::string::npos);

    // Pass attachment signatures are checked, not used as lossy hash keys.
    auto targetResult = context.device.create_texture({
        .extent = {16, 16, 1}, .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment,
    });
    TRUFFLE_CHECK(targetResult.ok());
    auto target = std::move(targetResult).value();
    TRUFFLE_CHECK(cache.prepare_render_pass({
        .extent = {16, 16}, .colorAttachments = {{.texture = &target}}}).ok());
    TRUFFLE_CHECK(!cache.get_or_create({}, 7).ok());
    TRUFFLE_CHECK(!cache.prepare_render_pass({
        .colorAttachments = {{.texture = nullptr}}}).ok());
    TRUFFLE_CHECK(cache.prepare_render_pass({}).ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 7).ok());
    cache.invalidate(7);
    TRUFFLE_CHECK(!cache.get_or_create({}, 7).ok());
    TRUFFLE_CHECK(!cache.get_or_create({}, 7, 42).ok());
    TRUFFLE_CHECK(cache.get_or_create({}, 8).ok());
    cache.invalidate_all();
    TRUFFLE_CHECK(!cache.get_or_create({}, 8).ok());
    return 0;
}

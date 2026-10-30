#include "renderer_native_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/rhi/metal_backend.hpp"
#include "truffle/renderer/mesh.hpp"

#include <fstream>
#include <iterator>

namespace {

using namespace truffle;

rhi::ShaderPackage make_package(bool invalidSource = false) {
    const auto path = std::string(TRUFFLE_RENDERER_FIXTURE_DIR) +
                      (invalidSource ? "/invalid.metal" : "/prepared.metal");
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read native Renderer fixture");
    }
    const std::string source{std::istreambuf_iterator<char>{input}, {}};
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    rhi::ShaderPackageDesc desc;
    desc.name = "prepared native Renderer";
    desc.sources = {{
        .path = invalidSource ? "invalid.metal" : "prepared.metal",
        .language = rhi::ShaderSourceLanguage::msl,
        .sha256 = invalidSource
                      ? "69fa9bbe6f5a57eb510d0c99603a787a97651eca2edfd5be2d96d7bbc05c6b5f"
                      : "458aea31af0e7ee053391e715bae853f882751fc864402f2826c729cd277ecfc",
    }};
    desc.compilers = {{.name = "Metal native source", .version = "host SDK"}};
    desc.permutations = {{.name = "configured"}};
    const auto add = [&](rhi::ShaderStage stage, const char* entry,
                          std::vector<rhi::ResourceBinding> bindings = {}) {
        desc.variants.push_back({
            .target = rhi::ShaderTarget::metal_source,
            .format = rhi::ShaderByteFormat::native_source,
            .kind = rhi::ShaderVariantKind::native_override,
            .stage = stage,
            .entryPoint = entry,
            .permutation = "configured",
            .reflection = {.bindings = std::move(bindings)},
            .code = {bytes.begin(), bytes.end()},
        });
    };
    add(rhi::ShaderStage::vertex, "vertex_main");
    add(rhi::ShaderStage::vertex, "attribute_vertex");
    add(rhi::ShaderStage::fragment, "red_fragment");
    add(rhi::ShaderStage::fragment, "green_fragment");
    add(rhi::ShaderStage::fragment, "tinted_fragment",
        {{.name = "tint", .stage = rhi::ShaderStage::fragment,
          .binding = 0, .minimumSize = 16}});
    add(rhi::ShaderStage::fragment, "scalar_fragment",
        {{.name = "tint", .stage = rhi::ShaderStage::fragment,
          .binding = 0, .minimumSize = 4}});
    auto package = tests::require(rhi::ShaderPackage::create(std::move(desc)));
    // Exercise the file-consumer format boundary as well as in-memory inputs.
    return tests::require(rhi::ShaderPackage::load(package.bytes()));
}

renderer::ProgramDesc program_desc(const rhi::ShaderPackage& package,
                                   const char* fragment = "red_fragment") {
    renderer::ProgramDesc desc;
    desc.vertex = {&package, rhi::ShaderTarget::metal_source, "vertex_main",
                   rhi::ShaderStage::vertex, "configured"};
    desc.fragment = {&package, rhi::ShaderTarget::metal_source, fragment,
                     rhi::ShaderStage::fragment, "configured"};
    desc.pipeline.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
    return desc;
}

int run() {
    // Native availability is required: failure on this supported host is not a skip.
    auto instance = tests::require(rhi::create_metal_instance());
    auto adapter = tests::require(instance.adapter(0));
    TRUFFLE_CHECK(adapter.info().native);
    auto device = tests::require(adapter.request_device());
    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    constexpr renderer::MaterialId material = 17;
    constexpr std::array red{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    constexpr std::array green{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}};
    constexpr std::array blue{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    constexpr std::array black{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    {
        auto package = make_package();
        {
            std::ofstream output(std::string(TRUFFLE_RENDERER_PACKAGE_OUTPUT_DIR) +
                                 "/prepared.tsp", std::ios::binary);
            const auto bytes = package.bytes();
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            TRUFFLE_CHECK(output.good());
        }
        tests::require(programs.register_program(material, program_desc(package)));
    }
    // Caller package, loaded byte buffer, descriptors and source strings are gone.
    auto target = tests::make_color_target(device);
    renderer::RenderBatch batch;
    batch.material = material;
    batch.instanceCount = 1;
    const auto render = [&](const auto& color, const char* label) {
        tests::require(renderer.render(tests::color_graph(target, batch)));
        tests::check_color(device, target, color, label);
    };
    render(red, "prepared TSP after caller destruction");
    const auto initialId = tests::require(programs.get_or_create({}, material))->id();
    const auto pipelineCount = instance.stats().pipelinesCreated;
    render(red, "cached program reuse");
    TRUFFLE_CHECK(tests::require(programs.get_or_create({}, material))->id() == initialId);
    TRUFFLE_CHECK(instance.stats().pipelinesCreated == pipelineCount);
    TRUFFLE_CHECK(renderer.last_frame_stats().renderNodesExecuted == 1);
    TRUFFLE_CHECK(renderer.last_frame_stats().renderBatchesExecuted == 1);

    auto package = make_package();
    auto greenDesc = program_desc(package, "green_fragment");
    auto candidate = tests::require(programs.stage_program(greenDesc));
    render(red, "staged replacement is not published");

    // Record the old pipeline, then drop every owning cache/shader wrapper by
    // committing the candidate before submission. RHI must retain the old payload.
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    auto encoder = tests::require(list.begin_rendering({
        .extent = {16, 16}, .colorAttachments = {{.texture = &target}},
    }));
    tests::require(encoder.bind_pipeline(
        *tests::require(programs.get_or_create({}, material))));
    tests::require(encoder.draw(3, 1));
    tests::require(encoder.end());
    tests::require(list.end());
    tests::require(programs.commit_program(material, std::move(candidate)));
    TRUFFLE_CHECK(tests::require(programs.get_or_create({}, material))->id() != initialId);
    tests::submit_graphics(device, list);
    tests::check_color(device, target, red, "recorded old pipeline survives replacement");
    render(green, "committed replacement");

    const auto greenId = tests::require(programs.get_or_create({}, material))->id();
    auto bad = greenDesc;
    bad.fragment.entryPoint = "missing";
    TRUFFLE_CHECK(!programs.register_program(material, bad).ok());
    render(green, "missing entry preserves output");
    bad = greenDesc;
    bad.fragment.target = rhi::ShaderTarget::spirv;
    TRUFFLE_CHECK(!programs.register_program(material, bad).ok());
    bad = greenDesc;
    bad.fragment.permutation = "missing";
    TRUFFLE_CHECK(!programs.register_program(material, bad).ok());
    bad = greenDesc;
    bad.vertex.stage = rhi::ShaderStage::compute;
    TRUFFLE_CHECK(!programs.register_program(material, bad).ok());
    render(green, "missing target/permutation and wrong stage preserve output");

    auto invalidPackage = make_package(true);
    const auto shaderFailure = programs.register_program(material, program_desc(invalidPackage));
    TRUFFLE_CHECK(!shaderFailure.ok() && !shaderFailure.message.empty());
    TRUFFLE_CHECK(shaderFailure.code == base::StatusCode::backend_error);
    TRUFFLE_CHECK(shaderFailure.detail.has_value());
    std::cout << "native shader failure: " << shaderFailure.message << '\n';
    render(green, "native shader failure preserves output");
    bad = greenDesc;
    bad.vertex.entryPoint = "attribute_vertex";
    const auto pipelineFailure = programs.register_program(material, bad);
    TRUFFLE_CHECK(!pipelineFailure.ok() && !pipelineFailure.message.empty());
    TRUFFLE_CHECK(pipelineFailure.code == base::StatusCode::backend_error);
    TRUFFLE_CHECK(pipelineFailure.detail.has_value());
    std::cout << "native pipeline failure: " << pipelineFailure.message << '\n';
    render(green, "native pipeline failure preserves output");
    TRUFFLE_CHECK(tests::require(programs.get_or_create({}, material))->id() == greenId);
    bad = greenDesc;
    bad.pipeline.colorTargets[0].format = rhi::TextureFormat::bgra8_unorm;
    TRUFFLE_CHECK(!programs.register_program(material, bad).ok());
    render(green, "incompatible attachment replacement preserves output");

    // Prepared configuration slots are independent; static state is never lost
    // behind a reused material/layout hash.
    tests::require(programs.register_program(material, program_desc(package), 42));
    batch.variantHash = 42;
    render(red, "independent prepared configuration");
    batch.variantHash = 0;
    auto masked = greenDesc;
    masked.pipeline.colorTargets[0].writeMask = 0;
    tests::require(programs.register_program(material, masked));
    render(black, "changed color write mask");
    tests::require(programs.register_program(material, greenDesc));
    render(green, "restored static graphics state");

    // Descriptor bindings stay backend-neutral. Pipeline staging validates
    // reflection/layouts, then per-batch groups carry resources and dynamic offsets.
    auto layout = tests::require(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = 16}},
    }));
    auto pipelineLayout = tests::require(device.create_pipeline_layout({
        .bindGroupLayouts = {&layout},
    }));
    auto tintDesc = program_desc(package, "tinted_fragment");
    TRUFFLE_CHECK(!programs.register_program(material, tintDesc).ok());
    render(green, "missing reflection layout preserves output");
    tintDesc.pipeline.layout = &pipelineLayout;
    auto changedLayout = tests::require(programs.stage_program(tintDesc));
    TRUFFLE_CHECK(!programs.commit_program(material, std::move(changedLayout)).ok());
    render(green, "pipeline-layout mismatch preserves output");
    tests::require(programs.commit_program(18, std::move(changedLayout)));
    const auto boundProgramId = tests::require(programs.get_or_create({}, 18))->id();
    auto scalarDesc = tintDesc;
    scalarDesc.fragment.entryPoint = "scalar_fragment";
    // Same exact layout, attachment state and binding count, different minimum
    // buffer size. Both pipelines are native-valid under the layout's 16 bytes.
    auto changedBindings = tests::require(programs.stage_program(scalarDesc));
    const auto bindingStatus = programs.commit_program(18, std::move(changedBindings));
    TRUFFLE_CHECK(bindingStatus.code == base::StatusCode::invalid_argument);
    TRUFFLE_CHECK(tests::require(programs.get_or_create({}, 18))->id() == boundProgramId);

    auto arena = tests::require(device.create_descriptor_arena());
    auto uniform = tests::require(device.create_buffer({
        .size = 16, .usage = rhi::BufferUsage::uniform,
        .memory = rhi::MemoryDomain::upload,
    }));
    const std::array tint{0.0F, 0.0F, 1.0F, 1.0F};
    tests::require(uniform.write(0, std::as_bytes(std::span{tint})));
    auto group = tests::require(device.create_bind_group({
        .layout = &layout, .arena = &arena,
        .entries = {{.binding = 0, .buffer = &uniform, .size = 16}},
    }));
    // Pipeline and descriptors retain caller-local layout payloads.
    pipelineLayout = {};
    layout = {};
    batch.material = 18;
    TRUFFLE_CHECK(!renderer.render(tests::color_graph(target, batch)).ok());
    batch.bindGroups = {{.bindings = &group}};
    render(blue, "reflected uniform survives same-layout reflection rejection");
    auto otherLayout = tests::require(device.create_bind_group_layout({
        .entries = {{.binding = 0, .type = rhi::BindingType::uniform_buffer,
                     .visibility = rhi::ShaderStageMask::fragment,
                     .minimumBufferSize = 16}},
    }));
    auto otherGroup = tests::require(device.create_bind_group({
        .layout = &otherLayout, .arena = &arena,
        .entries = {{.binding = 0, .buffer = &uniform, .size = 16}},
    }));
    batch.bindGroups = {{.bindings = &otherGroup}};
    TRUFFLE_CHECK(!renderer.render(tests::color_graph(target, batch)).ok());
    batch.bindGroups = {{.bindings = nullptr}};
    TRUFFLE_CHECK(!renderer.render(tests::color_graph(target, batch)).ok());
    batch.bindGroups.clear();
    batch.material = 404;
    TRUFFLE_CHECK(!renderer.render(tests::color_graph(target, batch)).ok());
    batch.material = material;
    render(green, "recovery after missing resources/material");
    auto meshProgram = program_desc(package, "green_fragment");
    meshProgram.vertex.entryPoint = "attribute_vertex";
    meshProgram.pipeline.vertexBuffers = {{
        .stride = 16, .attributes = {{.location = 0}},
    }};
    tests::require(programs.register_program(19, meshProgram));
    const std::array positions{
        std::array{-1.0F, -1.0F, 0.0F, 1.0F},
        std::array{3.0F, -1.0F, 0.0F, 1.0F},
        std::array{-1.0F, 3.0F, 0.0F, 1.0F},
    };
    const std::array streams{
        renderer::MeshStream{std::as_bytes(std::span{positions}), 16},
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    auto mesh = tests::require(renderer::Mesh::upload(device, {
        .streams = streams, .vertexCount = 3,
        .indices = std::as_bytes(std::span{indices}),
    }));
    batch = mesh.batch(19);
    render(green, "uploaded indexed mesh with application-defined layout");
    auto validBatch = batch;
    batch.bindings[0].size = 16;
    TRUFFLE_CHECK(!renderer.render(tests::color_graph(target, batch)).ok());
    batch = validBatch;
    render(green, "mesh recovery after rejected byte range");
    const std::array<std::uint32_t, 5> arguments{3, 1, 0, 0, 0};
    auto indirect = tests::require(device.create_buffer({
        .size = sizeof(arguments), .usage = rhi::BufferUsage::indirect,
        .memory = rhi::MemoryDomain::upload,
    }));
    tests::require(indirect.write(0, std::as_bytes(std::span{arguments})));
    batch.kind = renderer::DrawKind::IndirectIndexed;
    batch.indirectBuffer = {&indirect, 0, sizeof(arguments)};
    render(green, "indexed indirect mesh binds its index buffer");
    auto incompatibleMesh = meshProgram;
    incompatibleMesh.pipeline.vertexBuffers[0].stride = 32;
    TRUFFLE_CHECK(!programs.register_program(19, incompatibleMesh).ok());
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "Metal Renderer proof failed: " << error.what() << '\n';
        return 1;
    }
}

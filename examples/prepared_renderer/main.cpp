#include "truffle/renderer/renderer.hpp"
#if defined(TRUFFLE_EXAMPLE_METAL)
#include "truffle/rhi/metal_backend.hpp"
#endif
#if defined(TRUFFLE_EXAMPLE_VULKAN)
#include "truffle/rhi/vulkan_backend.hpp"
#endif

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using namespace truffle;

void check(base::Status status) {
    if (!status.ok()) {
        throw std::runtime_error(status.message);
    }
}

template <typename T>
T check(base::Result<T> result) {
    if (!result.ok()) {
        throw std::runtime_error(result.status().message);
    }
    return std::move(result).value();
}

rhi::ShaderPackage load_package(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(std::string("cannot read TSP: ") + path);
    }
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    return check(rhi::ShaderPackage::load(
        std::as_bytes(std::span{bytes.data(), bytes.size()})));
}

base::Result<rhi::Instance> create_instance(rhi::ShaderTarget target) {
#if defined(TRUFFLE_EXAMPLE_METAL)
    if (target == rhi::ShaderTarget::metal_source ||
        target == rhi::ShaderTarget::metal_library) {
        return rhi::create_metal_instance();
    }
#endif
#if defined(TRUFFLE_EXAMPLE_VULKAN)
    if (target == rhi::ShaderTarget::spirv) {
        return rhi::create_vulkan_instance();
    }
#endif
    return base::Status::failure(base::StatusCode::unsupported,
                                 "this example needs an enabled Metal or Vulkan target");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 7 && argc != 8) {
        std::cerr << "usage: truffle_prepared_renderer <msl|metallib|spirv> "
                     "<vertex.tsp> <vertex-entry> <fragment.tsp> <fragment-entry> "
                     "<configuration-slot-number> [permutation]\n";
        return 1;
    }
    try {
        const auto targetKind = check(rhi::shader_target_from_name(argv[1]));
        auto instance = check(create_instance(targetKind));
        auto adapter = check(instance.adapter(0));
        auto device = check(adapter.request_device());
        renderer::PipelineCache programs(device);
        renderer::Renderer renderer(device, &programs);
        constexpr renderer::MaterialId material = 1;
        const auto slot = static_cast<std::size_t>(std::stoull(argv[6]));
        {
            auto vertex = load_package(argv[2]);
            auto fragment = load_package(argv[4]);
            const std::string permutation = argc == 8 ? argv[7] : "";
            renderer::ProgramDesc desc;
            desc.vertex = {&vertex, targetKind, argv[3], rhi::ShaderStage::vertex, permutation};
            desc.fragment = {&fragment, targetKind, argv[5], rhi::ShaderStage::fragment, permutation};
            desc.pipeline.colorTargets = {{.format = rhi::TextureFormat::rgba8_unorm}};
            check(programs.register_program(material, desc, slot));
        }
        auto target = check(device.create_texture({
            .extent = {16, 16, 1}, .format = rhi::TextureFormat::rgba8_unorm,
            .usage = rhi::TextureUsage::color_attachment,
        }));
        auto pool = check(device.create_command_pool(rhi::QueueKind::graphics));
        auto list = check(pool.allocate());
        check(list.begin());
        rhi::BarrierBatch barriers;
        barriers.textures.push_back({
            .texture = &target, .oldLayout = rhi::TextureLayout::undefined,
            .newLayout = rhi::TextureLayout::color_attachment,
            .sourceStages = rhi::PipelineStage::top,
            .destinationStages = rhi::PipelineStage::color_attachment_output,
            .destinationAccess = rhi::Access::color_attachment_write,
        });
        check(list.barrier(barriers));
        check(list.end());
        auto queue = check(device.queue(rhi::QueueKind::graphics));
        const std::array<rhi::CommandList*, 1> lists{&list};
        check(queue.submit(lists));

        renderer::RenderBatch batch;
        batch.material = material;
        batch.variantHash = slot;
        batch.instanceCount = 1;
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{
                .extent = {16, 16}, .colorAttachments = {{.texture = &target}}},
            std::vector<renderer::RenderBatch>{batch}));
        check(renderer.render(graph));
        std::cout << "Rendered " << renderer.last_frame_stats().renderBatchesExecuted
                  << " prepared TSP batch after destroying caller packages\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

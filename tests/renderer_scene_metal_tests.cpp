#include "renderer_native_test_utils.hpp"
#include "scene.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <numbers>

namespace {

using namespace truffle;

rhi::Texture target(rhi::Device& device, rhi::Extent2D extent) {
    auto color = tests::require(device.create_texture({
        .extent = {extent.width, extent.height, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::undefined,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    tests::submit_graphics(device, list);
    return color;
}

void check_pixels(rhi::Device& device, rhi::Texture& color,
                  std::array<unsigned, 4> expected, const char* label) {
    const auto extent = color.desc().extent;
    const std::size_t pitch = (static_cast<std::size_t>(extent.width) * 4 + 255) & ~std::size_t{255};
    auto output = tests::require(device.create_buffer({
        .size = pitch * extent.height, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::color_attachment,
                         rhi::TextureLayout::transfer_source);
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_texture_to_buffer(color, output, {
        .layout = {.bytesPerRow = pitch, .rowsPerImage = extent.height},
        .texture = {.extent = extent},
    }));
    tests::require(copy.end());
    tests::color_barrier(list, color, rhi::TextureLayout::transfer_source,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    tests::submit_graphics(device, list);
    std::vector<std::byte> bytes(pitch * extent.height);
    tests::require(output.read(0, bytes));
    for (std::size_t y = 0; y < extent.height; ++y) {
        for (std::size_t x = 0; x < extent.width; ++x) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (std::to_integer<unsigned>(bytes[y * pitch + x * 4 + channel]) != expected[channel]) {
                    throw std::runtime_error(std::string(label) + ": unexpected pixel at " +
                                              std::to_string(x) + "," + std::to_string(y));
                }
            }
        }
    }
    std::cout << label << ": " << extent.width * extent.height
              << " exact pixels, red=" << expected[0] << " green=" << expected[1] << '\n';
}

int run() {
    auto instance = tests::require(rhi::create_metal_instance());
    auto adapter = tests::require(instance.adapter(0));
    auto device = tests::require(adapter.request_device());
    minimum_renderer::Scene scene(device, rhi::TextureFormat::rgba8_unorm);
    const std::array vertices{
        minimum_renderer::Vertex{{-1, -1, 0}, {0, 0, 1}},
        minimum_renderer::Vertex{{3, -1, 0}, {0, 0, 1}},
        minimum_renderer::Vertex{{-1, 3, 0}, {0, 0, 1}},
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    scene.set_mesh(vertices, indices);
    auto color = target(device, {16, 16});
    renderer::DepthTarget depth(device);
    tests::require(depth.resize({16, 16}));
    minimum_renderer::View view;
    view.viewProjection = tests::require(renderer::orthographic(2, 2, 0, 10));
    scene.set_view(view);
    const minimum_renderer::Object front{
        .centerScale = {0, 0, -1, 1}, .albedo = {1, 0, 0, 1},
    };
    const minimum_renderer::Object back{
        .centerScale = {0, 0, -2, 1}, .albedo = {0, 1, 0, 1},
    };
    const auto draw = [&](std::array<unsigned, 4> expected, const char* label) {
        renderer::FrameGraph graph;
        const auto size = color.desc().extent;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{
                .extent = {size.width, size.height},
                .colorAttachments = {{.texture = &color}},
                .depthStencilAttachment = {.texture = depth.texture()},
            },
            std::vector{scene.batch()}));
        scene.render(graph);
        check_pixels(device, color, expected, label);
    };
    scene.set_objects(std::array{front, back});
    draw({255, 0, 0, 255}, "front object occludes later back instance");
    scene.set_objects(std::array{back, front});
    draw({255, 0, 0, 255}, "depth independent of instance order");
    scene.set_objects(std::array{back});
    draw({0, 255, 0, 255}, "application-defined instance color and position");
    scene.set_objects(std::array{front, back});
    view.lightDirectionIntensity[3] = 0.5F;
    scene.set_view(view);
    draw({128, 0, 0, 255}, "composed directional intensity");
    view.lightDirectionIntensity = {0, 0, 1, 1};
    scene.set_view(view);
    draw({0, 0, 0, 255}, "reversed directional light");
    auto rotated = front;
    rotated.orientation = {0, 1, 0, 0};
    scene.set_objects(std::array{rotated});
    draw({255, 0, 0, 255}, "application quaternion rotates geometry and normals");
    scene.set_objects(std::array{front, back});
    view.lightDirectionIntensity = {0, 0, -1, 1};
    view.viewProjection = renderer::multiply(
        tests::require(renderer::orthographic(2, 2, 0, 10)),
        tests::require(renderer::look_at({10, 0, 0}, {10, 0, -1})));
    scene.set_view(view);
    draw({0, 0, 0, 255}, "camera translation removes objects from view");
    view.viewProjection = tests::require(renderer::perspective(
        std::numbers::pi_v<float> / 2, 1, 0.1F, 10));
    scene.set_view(view);
    draw({255, 0, 0, 255}, "perspective projection and depth");
    color = target(device, {32, 24});
    tests::require(depth.resize({32, 24}));
    view.viewProjection = tests::require(renderer::orthographic(2, 2, 0, 10));
    scene.set_view(view);
    draw({255, 0, 0, 255}, "recreated color and depth targets");
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "Minimum Renderer Metal proof failed: " << error.what() << '\n';
        return 1;
    }
}

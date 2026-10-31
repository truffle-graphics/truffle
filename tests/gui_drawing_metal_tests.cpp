#include "renderer_native_test_utils.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/painter.hpp"
#include "truffle/renderer/frame_stream.hpp"
#include "truffle/rhi/metal_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>

namespace {

using namespace truffle;
using gui::Color;
using gui::Decoration;
using gui::ElementId;
using gui::Rect;

constexpr std::uint32_t kSize = 32;
using Pixel = std::array<int, 4>;

void wait_graphics(rhi::Device& device, rhi::CommandList& list) {
    auto fence = tests::require(device.create_fence());
    auto queue = tests::require(device.queue(rhi::QueueKind::graphics));
    const std::array<rhi::CommandList*, 1> lists{&list};
    tests::require(queue.submit({.commandLists = lists, .signalFence = &fence,
                                 .signalFenceValue = 1}));
    tests::require(fence.wait(1, std::chrono::seconds(10)));
}

rhi::Texture make_target(rhi::Device& device) {
    auto color = tests::require(device.create_texture({
        .extent = {kSize, kSize, 1},
        .format = rhi::TextureFormat::rgba8_unorm,
        .usage = rhi::TextureUsage::color_attachment | rhi::TextureUsage::copy_source,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::undefined,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    wait_graphics(device, list);
    return color;
}

std::vector<std::byte> read_pixels(rhi::Device& device, rhi::Texture& color) {
    const std::size_t pitch = 256;
    auto output = tests::require(device.create_buffer({
        .size = pitch * kSize, .usage = rhi::BufferUsage::copy_destination,
        .memory = rhi::MemoryDomain::readback,
    }));
    auto pool = tests::require(device.create_command_pool(rhi::QueueKind::graphics));
    auto list = tests::require(pool.allocate());
    tests::require(list.begin());
    tests::color_barrier(list, color, rhi::TextureLayout::color_attachment,
                         rhi::TextureLayout::transfer_source);
    auto copy = tests::require(list.begin_copy());
    tests::require(copy.copy_texture_to_buffer(color, output, {
        .layout = {.bytesPerRow = pitch, .rowsPerImage = kSize},
        .texture = {.extent = {kSize, kSize, 1}},
    }));
    tests::require(copy.end());
    tests::color_barrier(list, color, rhi::TextureLayout::transfer_source,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    wait_graphics(device, list);
    std::vector<std::byte> bytes(pitch * kSize);
    tests::require(output.read(0, bytes));
    return bytes;
}

// CPU reference of the GUI program: premultiplied "over" blending of
// signed-distance-covered rounded rectangles, quantized like an rgba8 target.
struct Layer {
    Rect rect; // absolute pixels
    Decoration look;
    Rect clip{0, 0, kSize, kSize};
    std::function<Color(float u)> texel = {}; // null = white
};

float rounded_box(float px, float py, float hw, float hh, float radius) {
    const float qx = std::fabs(px) - hw + radius;
    const float qy = std::fabs(py) - hh + radius;
    const float outside = std::hypot(std::max(qx, 0.0F), std::max(qy, 0.0F));
    return outside + std::min(std::max(qx, qy), 0.0F) - radius;
}

std::vector<Pixel> reference(const std::vector<Layer>& layers) {
    std::vector<Pixel> image(kSize * kSize, Pixel{0, 0, 0, 255});
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            std::array<float, 4> dst{};
            for (std::size_t c = 0; c < 4; ++c) {
                dst[c] = static_cast<float>(image[y * kSize + x][c]) / 255.0F;
            }
            for (const Layer& layer : layers) {
                const float fx = static_cast<float>(x) + 0.5F;
                const float fy = static_cast<float>(y) + 0.5F;
                const Rect& clip = layer.clip;
                if (fx < clip.x || fy < clip.y || fx > clip.x + clip.width ||
                    fy > clip.y + clip.height) {
                    continue;
                }
                const float hw = layer.rect.width * 0.5F;
                const float hh = layer.rect.height * 0.5F;
                const float lx = fx - (layer.rect.x + hw);
                const float ly = fy - (layer.rect.y + hh);
                const float radius = std::min(layer.look.corner_radius, std::min(hw, hh));
                const float coverage =
                    std::clamp(0.5F - rounded_box(lx, ly, hw, hh, radius), 0.0F, 1.0F);
                Color fill = layer.look.fill;
                if (layer.texel) {
                    const Color t = layer.texel((lx + hw) / layer.rect.width);
                    fill = {fill.r * t.r, fill.g * t.g, fill.b * t.b, fill.a * t.a};
                }
                std::array<float, 4> src{fill.r * fill.a, fill.g * fill.a, fill.b * fill.a,
                                         fill.a};
                const float bw = layer.look.border_width;
                if (bw > 0.0F) {
                    const Color& e = layer.look.border;
                    const std::array<float, 4> edge{e.r * e.a, e.g * e.a, e.b * e.a, e.a};
                    const float inside = std::clamp(
                        0.5F - rounded_box(lx, ly, std::max(hw - bw, 0.0F),
                                           std::max(hh - bw, 0.0F),
                                           std::max(radius - bw, 0.0F)),
                        0.0F, 1.0F);
                    for (std::size_t c = 0; c < 4; ++c) {
                        src[c] = edge[c] + (src[c] - edge[c]) * inside;
                    }
                }
                for (std::size_t c = 0; c < 4; ++c) {
                    const float blended = src[c] * coverage + dst[c] * (1.0F - src[3] * coverage);
                    dst[c] = std::round(blended * 255.0F) / 255.0F;
                }
            }
            for (std::size_t c = 0; c < 4; ++c) {
                image[y * kSize + x][c] = static_cast<int>(std::lround(dst[c] * 255.0F));
            }
        }
    }
    return image;
}

void compare(const std::vector<std::byte>& bytes, const std::vector<Pixel>& expected,
             const char* label) {
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            for (std::size_t c = 0; c < 4; ++c) {
                const int actual = std::to_integer<int>(bytes[y * 256 + x * 4 + c]);
                const int want = expected[y * kSize + x][c];
                if (std::abs(actual - want) > 2) {
                    throw std::runtime_error(std::string(label) + ": pixel " +
                        std::to_string(x) + "," + std::to_string(y) + " channel " +
                        std::to_string(c) + " is " + std::to_string(actual) +
                        ", expected " + std::to_string(want));
                }
            }
        }
    }
    std::cout << label << ": " << kSize * kSize << " pixels match the reference\n";
}

void expect_pixel(const std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t y,
                  Pixel want, const char* what) {
    for (std::size_t c = 0; c < 4; ++c) {
        if (std::abs(std::to_integer<int>(bytes[y * 256 + x * 4 + c]) - want[c]) > 1) {
            throw std::runtime_error(std::string("unexpected pixel: ") + what);
        }
    }
}

ElementId make(gui::Context& context, ElementId parent, Rect rect, const Decoration& look) {
    const ElementId element = tests::require(context.create(parent));
    gui::LayoutParams params;
    params.width = rect.width;
    params.height = rect.height;
    params.position = {rect.x, rect.y};
    tests::require(context.set_layout_params(element, params));
    tests::require(context.set_decoration(element, look));
    return element;
}

Decoration fill(Color color) {
    Decoration look;
    look.fill = color;
    return look;
}

int run() {
    auto instance = tests::require(rhi::create_metal_instance());
    auto adapter = tests::require(instance.adapter(0));
    auto device = tests::require(adapter.request_device());

    renderer::PipelineCache programs(device);
    renderer::Renderer renderer(device, &programs);
    auto pacer = tests::require(renderer::FramePacer::create(device, 2));
    auto painter = tests::require(gui::Painter::create(device, programs));
    auto target = make_target(device);

    // 2x1 image: white | red, sampled nearest.
    const std::array<std::byte, 8> texels{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto image = tests::require(renderer::SampledTexture::upload(device, {
        .extent = {2, 1}, .bytes = texels}));
    const gui::TextureId image_id = tests::require(painter.add_texture(image));
    const auto image_texel = [](float u) {
        return u < 0.5F ? Color{1, 1, 1, 1} : Color{1, 0, 0, 1};
    };

    gui::Context context;
    const ElementId root = context.root();
    Decoration framed = fill({1, 0, 0, 1});
    framed.border = {0, 1, 0, 1};
    framed.border_width = 2;
    const ElementId a = make(context, root, {2, 2, 12, 12}, framed);
    const ElementId c = make(context, root, {8, 8, 8, 8}, fill({1, 1, 1, 1}));
    const ElementId b = make(context, root, {16, 2, 12, 12}, fill({0, 0, 1, 0.5F}));
    static_cast<void>(make(context, b, {8, 0, 12, 12}, fill({1, 1, 0, 1})));
    Decoration rounded = fill({1, 1, 1, 1});
    rounded.corner_radius = 4;
    static_cast<void>(make(context, root, {2, 18, 12, 12}, rounded));
    Decoration textured = fill({1, 1, 1, 1});
    textured.texture = image_id;
    static_cast<void>(make(context, root, {18, 18, 8, 8}, textured));

    const auto draw = [&](std::size_t expected_regenerated) {
        static_cast<void>(context.update_layout({kSize, kSize}));
        const auto shapes = context.update_shapes();
        if (shapes.regenerated != expected_regenerated) {
            throw std::runtime_error("regenerated " + std::to_string(shapes.regenerated) +
                                     " shapes, expected " +
                                     std::to_string(expected_regenerated));
        }
        const auto slot = tests::require(pacer.begin_frame());
        auto batches = tests::require(painter.record(context, slot, {kSize, kSize}));
        renderer::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::RenderPassNode>(
            rhi::RenderPassDesc{
                .extent = {kSize, kSize},
                .colorAttachments = {{.texture = &target, .clear = {0, 0, 0, 1}}},
            },
            std::move(batches)));
        const auto status = renderer.render(graph, nullptr, pacer.signal());
        pacer.end_frame(renderer.last_frame_stats().submitted);
        tests::require(status);
        tests::require(pacer.wait_idle());
        return read_pixels(device, target);
    };

    const auto layers = [&](Layer c_layer) {
        return std::vector<Layer>{
            {{2, 2, 12, 12}, framed},
            c_layer,
            {{16, 2, 12, 12}, fill({0, 0, 1, 0.5F})},
            {{24, 2, 12, 12}, fill({1, 1, 0, 1}), {16, 2, 12, 12}},
            {{2, 18, 12, 12}, rounded},
            {{18, 18, 8, 8}, textured, {0, 0, kSize, kSize}, image_texel},
        };
    };

    // Colors, borders, overlap order, clipping, rounded corners and textures.
    auto pixels = draw(7);
    if (painter.last_stats().elements != 6) {
        throw std::runtime_error("expected six decorated elements");
    }
    compare(pixels, reference(layers({{8, 8, 8, 8}, fill({1, 1, 1, 1})})),
            "GUI shapes, clipping and order");
    expect_pixel(pixels, 2, 2, {0, 255, 0, 255}, "border colour");
    expect_pixel(pixels, 5, 5, {255, 0, 0, 255}, "fill colour");
    expect_pixel(pixels, 12, 12, {255, 255, 255, 255}, "later sibling drawn on top");
    expect_pixel(pixels, 17, 5, {0, 0, 128, 255}, "translucent fill blended");
    expect_pixel(pixels, 27, 5, {255, 255, 0, 255}, "child inside parent clip");
    expect_pixel(pixels, 29, 5, {0, 0, 0, 255}, "child clipped by parent");
    expect_pixel(pixels, 2, 18, {0, 0, 0, 255}, "rounded corner left empty");
    expect_pixel(pixels, 19, 20, {255, 255, 255, 255}, "left texel");
    expect_pixel(pixels, 24, 20, {255, 0, 0, 255}, "right texel");

    // Unchanged elements are redrawn from stored shapes.
    pixels = draw(0);
    compare(pixels, reference(layers({{8, 8, 8, 8}, fill({1, 1, 1, 1})})),
            "unchanged frame without shape regeneration");

    // A new decoration regenerates one element.
    tests::require(context.set_decoration(c, fill({0, 0, 1, 1})));
    pixels = draw(1);
    compare(pixels, reference(layers({{8, 8, 8, 8}, fill({0, 0, 1, 1})})),
            "one redecorated element");

    // Moving keeps its shapes; it still draws before the textured element.
    gui::LayoutParams moved = tests::require(context.layout_params(c));
    moved.position = {20, 20};
    tests::require(context.set_layout_params(c, moved));
    pixels = draw(0);
    compare(pixels, reference(layers({{20, 20, 8, 8}, fill({0, 0, 1, 1})})),
            "moved element keeps its shapes");

    tests::require(painter.remove_texture(image_id));
    if (painter.record(context, tests::require(pacer.begin_frame()), {kSize, kSize}).ok()) {
        throw std::runtime_error("an unregistered texture must fail recording");
    }
    pacer.end_frame(false);
    static_cast<void>(a);
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "GUI drawing Metal test failed: " << error.what() << '\n';
        return 1;
    }
}

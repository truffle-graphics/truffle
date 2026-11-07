#include "renderer_native_test_utils.hpp"
#include "gui_text_fixture.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/menus.hpp"
#include "truffle/gui/painter.hpp"
#include "truffle/gui/widgets.hpp"
#include "truffle/renderer/specialist/frame_stream.hpp"
#if TFL_TEST_GUI_VULKAN
#include "truffle/rhi/vulkan_backend.hpp"
#include "vulkan_backend_test.hpp"
#else
#include "truffle/rhi/metal_backend.hpp"
#endif

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

rhi::Texture make_target(rhi::Device& device, rhi::Extent2D extent = {kSize, kSize}) {
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
    wait_graphics(device, list);
    return color;
}

std::vector<std::byte> read_pixels(rhi::Device& device, rhi::Texture& color,
                                 rhi::Extent2D extent = {kSize, kSize}) {
    const std::size_t pitch = (static_cast<std::size_t>(extent.width) * 4 + 255) / 256 * 256;
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
        .texture = {.extent = {extent.width, extent.height, 1}},
    }));
    tests::require(copy.end());
    tests::color_barrier(list, color, rhi::TextureLayout::transfer_source,
                         rhi::TextureLayout::color_attachment);
    tests::require(list.end());
    wait_graphics(device, list);
    std::vector<std::byte> bytes(pitch * extent.height);
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
             const char* label, int tolerance = 2) {
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            for (std::size_t c = 0; c < 4; ++c) {
                const int actual = std::to_integer<int>(bytes[y * 256 + x * 4 + c]);
                const int want = expected[y * kSize + x][c];
                if (std::abs(actual - want) > tolerance) {
                    throw std::runtime_error(std::string(label) + ": pixel " +
                        std::to_string(x) + "," + std::to_string(y) + " channel " +
                        std::to_string(c) + " is " + std::to_string(actual) +
                        ", expected " + std::to_string(want));
                }
            }
        }
    }
    std::cout << label << ": " << kSize * kSize
              << (tolerance == 0 ? " pixels match exactly\n" : " pixels match the reference\n");
}

void expect_pixel(const std::vector<std::byte>& bytes, std::uint32_t x, std::uint32_t y,
                  Pixel want, const char* what) {
    for (std::size_t c = 0; c < 4; ++c) {
        const int actual = std::to_integer<int>(bytes[y * 256 + x * 4 + c]);
        if (std::abs(actual - want[c]) > 1) {
            throw std::runtime_error(std::string("unexpected pixel: ") + what +
                " at " + std::to_string(x) + "," + std::to_string(y) +
                " channel " + std::to_string(c) + " is " + std::to_string(actual) +
                ", expected " + std::to_string(want[c]));
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
#if TFL_TEST_GUI_VULKAN
    auto instance = tests::require(rhi::create_vulkan_instance({.enableValidation = true}));
    if (rhi::detail::last_vulkan_validation_state_for_testing() !=
        rhi::detail::VulkanValidationState::active) {
        throw std::runtime_error("GUI Vulkan evidence requires active Khronos validation");
    }
#else
    auto instance = tests::require(rhi::create_metal_instance());
#endif
    auto adapter = tests::require(instance.adapter(0));
    if (!adapter.info().native) {
        throw std::runtime_error("GUI native evidence requires a native adapter");
    }
    std::cout << "GUI native adapter: " << adapter.info().name << '\n';
    auto device = tests::require(adapter.request_device());

#if TFL_TEST_GUI_TEXT
    auto text_engine = tests::require(gui::TextEngine::create(device, {{64, 64}}));
    const auto font_bytes = tests::gui_test_font();
    const auto font = tests::require(text_engine.load_font(font_bytes, 7.0F));
    const auto text_layout = tests::require(text_engine.shape(font, "A"));
#endif
    renderer::specialist::PipelineCache programs(device);
    renderer::specialist::Renderer renderer(device, &programs);
    auto pacer = tests::require(renderer::specialist::FramePacer::create(device, 2));
    auto painter = tests::require(gui::Painter::create(device, programs));
    auto target = make_target(device);

    // 2x1 image: white | red, sampled nearest.
    const std::array<std::byte, 8> texels{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto image = tests::require(renderer::specialist::SampledTexture::upload(device, {
        .extent = {2, 1}, .bytes = texels}));
    const gui::TextureId image_id = tests::require(painter.add_texture(image));
#if TFL_TEST_GUI_TEXT
    const gui::TextureId atlas_id =
        tests::require(painter.add_texture(tests::require(text_engine.atlas_texture()).get()));
#endif
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

    const auto render_context = [&](gui::Context& tree, std::size_t expected_regenerated,
                                    float scale = 1.0F) {
        tests::require(tree.set_viewport({kSize / scale, kSize / scale}, scale));
        const auto frame = tree.prepare_frame(gui::Context::Clock::now());
        tests::require(frame.status);
        if (frame.shapes.regenerated != expected_regenerated) {
            throw std::runtime_error("regenerated " + std::to_string(frame.shapes.regenerated) +
                                     " shapes, expected " +
                                     std::to_string(expected_regenerated));
        }
        const auto slot = tests::require(pacer.begin_frame());
        auto batches = tests::require(painter.record(tree, slot, {kSize, kSize}));
        renderer::specialist::FrameGraph graph;
        graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
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
    const auto draw = [&](std::size_t expected_regenerated) {
        return render_context(context, expected_regenerated);
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

#if TFL_TEST_GUI_TEXT
    const ElementId text_element = tests::require(context.create(root));
    gui::LayoutParams text_params;
    text_params.position = {2, 2};
    tests::require(context.set_layout_params(text_element, text_params));
    tests::require(context.set_text(text_element, text_layout, atlas_id, {0, 1, 1, 1}));
    pixels = draw(1);
    expect_pixel(pixels, 3, 2, {0, 255, 255, 255}, "FreeType glyph rendered from atlas");
    pixels = draw(0);
    expect_pixel(pixels, 3, 2, {0, 255, 255, 255}, "unchanged glyph retained in atlas");
#endif

    const auto widget_theme = [](gui::Context& tree) {
        tests::require(tree.set_theme_value("gui.surface", Color{0, 0, 1, 1}));
        tests::require(tree.set_theme_value("gui.control", Color{1, 0, 0, 1}));
        tests::require(tree.set_theme_value("gui.hover", Color{0, 1, 0, 1}));
        tests::require(tree.set_theme_value("gui.pressed", Color{0, 0, 1, 1}));
        tests::require(tree.set_theme_value("gui.accent", Color{1, 1, 0, 1}));
        tests::require(tree.set_theme_value("gui.text", Color{0, 1, 1, 1}));
        tests::require(tree.set_theme_value("gui.radius", 0.0F));
        tests::require(tree.set_theme_value("gui.border_width", 0.0F));
        tests::require(tree.set_theme_value("gui.padding", gui::Edges{}));
    };
    const auto place_widget = [](gui::Context& tree, ElementId id, Rect rect) {
        gui::LayoutParams params;
        params.position = {rect.x, rect.y};
        params.width = rect.width;
        params.height = rect.height;
        tests::require(tree.set_layout_params(id, params));
    };
    {
        gui::Context tree;
        widget_theme(tree);
        auto panel = tests::require(gui::Widget::create(tree, tree.root()));
        place_widget(tree, panel.element(), {2, 2, 20, 20});
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 5, 5, {0, 0, 255, 255}, "panel widget");
        expect_pixel(pixels, 24, 5, {0, 0, 0, 255}, "panel outside");
    }
#if TFL_TEST_GUI_TEXT
    {
        gui::Context tree;
        widget_theme(tree);
        auto label = tests::require(gui::Widget::create(tree, tree.root(), {
            .kind = gui::WidgetKind::label, .text = "A", .text_engine = &text_engine,
            .font = font, .atlas = atlas_id}));
        place_widget(tree, label.element(), {2, 2, 10, 10});
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 3, 2, {0, 255, 255, 255}, "label widget atlas glyph");
        tests::require(label.set_text("B"));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 2, 2, {0, 255, 255, 255}, "label updated glyph");
    }
    {
        gui::Context tree;
        tests::require(tree.set_theme_value("gui.surface", Color{0, 0, 0, 1}));
        tests::require(tree.set_theme_value("gui.text", Color{1, 1, 1, 1}));
        tests::require(tree.set_theme_value("gui.selection",
                                            Color{35 / 255.0F, 70 / 255.0F, 126 / 255.0F, 1}));
        tests::require(tree.set_theme_value("gui.caret", Color{1, 1, 1, 1}));
        tests::require(tree.set_theme_value("gui.composition",
                                            Color{217 / 255.0F, 217 / 255.0F, 1, 1}));
        tests::require(tree.set_theme_value("gui.radius", 0.0F));
        tests::require(tree.set_theme_value("gui.border_width", 0.0F));
        auto edit = tests::require(gui::TextEdit::create(tree, tree.root(), {
            .text = " A", .text_engine = &text_engine, .font = font, .atlas = atlas_id}));
        place_widget(tree, edit.element(), {2, 2, 24, 16});
        tests::require(tree.set_decoration(edit.element(), {.fill = {0, 0, 0, 1}}));
        tests::require(tree.focus(edit.element()));
        tests::require(edit.select({0, 1}));
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 3, 4, {35, 70, 126, 255}, "text editor selection background");
        expect_pixel(pixels, 5, 4, {255, 255, 255, 255}, "text editor caret");
        pixels = render_context(tree, 0);
        expect_pixel(pixels, 3, 4, {35, 70, 126, 255}, "cached text editor selection");
        tests::require(tree.handle(gui::CompositionEvent{
            .preedit = " ", .selection_begin = 1, .selection_end = 1}));
        pixels = render_context(tree, 2);
        expect_pixel(pixels, 3, 8, {217, 217, 255, 255}, "input-method preedit underline");
        if (tests::require(edit.text()) != " A") {
            throw std::runtime_error("preedit changed the committed pixel-test document");
        }
    }
#endif
    {
        gui::Context tree;
        widget_theme(tree);
        auto image_widget = tests::require(gui::Widget::create(tree, tree.root(), {
            .kind = gui::WidgetKind::image, .image = image_id, .image_size = {8, 8}}));
        place_widget(tree, image_widget.element(), {2, 2, 8, 8});
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 3, 4, {255, 255, 255, 255}, "image widget white texel");
        expect_pixel(pixels, 8, 4, {255, 0, 0, 255}, "image widget red texel");
    }
    {
        gui::Context tree;
        widget_theme(tree);
        int clicks = 0;
        auto button = tests::require(gui::Widget::create(tree, tree.root(), {
            .kind = gui::WidgetKind::button, .activated = [&] { ++clicks; }}));
        place_widget(tree, button.element(), {2, 2, 24, 24});
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 15, 15, {255, 0, 0, 255}, "button normal");
        tests::require(tree.handle(gui::PointerEvent{.position = {15, 15}}));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 15, 15, {0, 255, 0, 255}, "button hovered");
        tests::require(tree.handle(gui::PointerEvent{.action = gui::PointerAction::down,
            .position = {15, 15}, .button = gui::PointerButton::primary}));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 15, 15, {0, 0, 255, 255}, "button pressed");
        tests::require(tree.handle(gui::PointerEvent{.action = gui::PointerAction::up,
            .position = {15, 15}, .button = gui::PointerButton::primary}));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 15, 15, {0, 255, 0, 255}, "button released");
        if (clicks != 1) { throw std::runtime_error("native button did not activate once"); }
    }
    {
        gui::Context tree;
        widget_theme(tree);
        auto check = tests::require(gui::Widget::create(tree, tree.root(),
                                                       {.kind = gui::WidgetKind::checkbox}));
        place_widget(tree, check.element(), {0, 0, 32, 24});
        tests::require(tree.set_property_override(check.element(), gui::Property::padding,
                                                  gui::Edges{24, 0, 0, 0}));
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 6, 6, {255, 0, 0, 255}, "checkbox unchecked");
        tests::require(check.set_checked(true));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 6, 6, {255, 255, 0, 255}, "checkbox checked");
    }
    {
        gui::Context tree;
        widget_theme(tree);
        auto slider = tests::require(gui::Widget::create(tree, tree.root(),
                                                         {.kind = gui::WidgetKind::slider}));
        place_widget(tree, slider.element(), {2, 2, 24, 16});
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 8, 10, {255, 255, 0, 255}, "slider at minimum");
        tests::require(slider.set_value(1));
        pixels = render_context(tree, 1);
        expect_pixel(pixels, 20, 10, {255, 255, 0, 255}, "slider at maximum");
    }
    {
        gui::Context tree;
        widget_theme(tree);
        auto scroll = tests::require(gui::Widget::create(tree, tree.root(),
                                             {.kind = gui::WidgetKind::scroll_area}));
        place_widget(tree, scroll.element(), {2, 2, 16, 16});
        static_cast<void>(make(tree, scroll.content(), {0, 0, 16, 16}, fill({1, 0, 0, 1})));
        static_cast<void>(make(tree, scroll.content(), {0, 16, 16, 16}, fill({0, 1, 0, 1})));
        pixels = render_context(tree, tree.element_count());
        expect_pixel(pixels, 4, 4, {255, 0, 0, 255}, "scroll area initial content");
        expect_pixel(pixels, 4, 20, {0, 0, 0, 255}, "scroll area clips content");
        tests::require(scroll.set_scroll({0, 16}));
        pixels = render_context(tree, 0);
        expect_pixel(pixels, 4, 4, {0, 255, 0, 255}, "scroll area translated content");
        expect_pixel(pixels, 4, 20, {0, 0, 0, 255}, "translated content remains clipped");
        if (tree.hit_test({4, 4}) != tree.children(scroll.content()).back()) {
            throw std::runtime_error("scroll hit test disagrees with native drawing");
        }
    }
    std::cout << "Core widgets: native panel, label, image, button, checkbox, slider "
                 "and scroll-area pixel evidence\n";

    {
        constexpr rhi::Extent2D extent{512, 240};
        auto menu_target = make_target(device, extent);
        gui::Context tree;
        widget_theme(tree);
        tests::require(tree.set_theme_value("gui.disabled", Color{64 / 255.0F,
                                                                  64 / 255.0F, 64 / 255.0F, 1}));
        tests::require(tree.set_theme_value("gui.text_disabled", Color{1, 0, 1, 1}));
        tests::require(tree.set_theme_value("gui.border", Color{1, 1, 0, 1}));
        tests::require(tree.set_theme_value("gui.padding", gui::Edges{8, 8, 8, 8}));
        tests::require(tree.set_viewport({512, 240}));
        gui::MenuTextStyle style;
        std::string label;
#if TFL_TEST_GUI_TEXT
        style = {&text_engine, font, atlas_id};
        label = "A";
#endif
        auto button = tests::require(gui::MenuButton::create(tree, tree.root(), label, style));
        place_widget(tree, button.element(), {10, 10, 80, 28});
        auto menu = tests::require(button.menu());
        auto child = tests::require(menu.add_submenu(label));
        auto separator = tests::require(menu.add_separator());
        int actions = 0;
        auto disabled = tests::require(menu.add_item({.text = label, .enabled = false,
            .shortcut = label,
            .activated = [&] { actions += 100; }}));
        auto checked = tests::require(menu.add_item({.text = label, .checked = true,
            .shortcut = label, .activated = [&] { ++actions; }}));
        auto leaf = tests::require(child.add_item({.text = label, .activated = [&] { ++actions; }}));
        auto grandchild = tests::require(child.add_submenu(label));
        auto deep = tests::require(grandchild.add_item({.text = label, .checked = true,
            .activated = [&] { ++actions; }}));
        const auto draw_menu = [&] {
            tests::require(tree.prepare_frame(gui::Context::Clock::now()).status);
            const auto slot = tests::require(pacer.begin_frame());
            auto batches = tests::require(painter.record(tree, slot, extent));
            renderer::specialist::FrameGraph graph;
            graph.add_node(std::make_unique<renderer::specialist::RenderPassNode>(
                rhi::RenderPassDesc{
                    .extent = extent,
                    .colorAttachments = {{.texture = &menu_target, .clear = {0, 0, 0, 1}}},
                }, std::move(batches)));
            const auto status = renderer.render(graph, nullptr, pacer.signal());
            pacer.end_frame(renderer.last_frame_stats().submitted);
            tests::require(status);
            tests::require(pacer.wait_idle());
            return read_pixels(device, menu_target, extent);
        };
        const auto expect = [&](const std::vector<std::byte>& bytes, gui::Point point,
                                Pixel wanted, const char* what) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 ||
                point.y < 0 || point.x >= static_cast<float>(extent.width) ||
                point.y >= static_cast<float>(extent.height)) {
                throw std::runtime_error(std::string(what) + ": probe is outside the viewport");
            }
            const auto x = static_cast<std::uint32_t>(point.x);
            const auto y = static_cast<std::uint32_t>(point.y);
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const int actual = std::to_integer<int>(
                    bytes[(static_cast<std::size_t>(y) * extent.width + x) * 4 + channel]);
                if (actual != wanted[channel]) {
                    throw std::runtime_error(std::string(what) + ": pixel " +
                        std::to_string(x) + "," + std::to_string(y) + " channel " +
                        std::to_string(channel) + " is " + std::to_string(actual) +
                        ", expected " + std::to_string(wanted[channel]));
                }
            }
        };
        const auto ink = [&](const std::vector<std::byte>& bytes, ElementId element) {
            const auto rect = tests::require(tree.absolute_bounds(element));
            if (rect.x < 0 || rect.y < 0 || rect.x + rect.width > static_cast<float>(extent.width) ||
                rect.y + rect.height > static_cast<float>(extent.height)) {
                throw std::runtime_error("native menu indicator is outside the viewport");
            }
            std::size_t count = 0;
            for (auto y = static_cast<std::uint32_t>(rect.y);
                 y < static_cast<std::uint32_t>(rect.y + rect.height); ++y) {
                for (auto x = static_cast<std::uint32_t>(rect.x);
                     x < static_cast<std::uint32_t>(rect.x + rect.width); ++x) {
                    const auto offset = (static_cast<std::size_t>(y) * extent.width + x) * 4;
                    if (bytes[offset] == std::byte{0} && bytes[offset + 1] == std::byte{255} &&
                        bytes[offset + 2] == std::byte{255} && bytes[offset + 3] == std::byte{255}) {
                        ++count;
                    }
                }
            }
            return count;
        };
        const auto row_probe = [&](ElementId row) {
            const auto rect = tests::require(tree.absolute_bounds(row));
            return gui::Point{rect.x + 70, rect.y + rect.height * 0.5F};
        };
        auto menu_pixels = draw_menu();
        expect(menu_pixels, {30, 55}, {0, 0, 0, 255}, "closed menu draws nothing");
        tests::require(tree.focus(button.element()));
        tests::require(menu.open());
        menu_pixels = draw_menu();
        const auto popup = tests::require(tree.absolute_bounds(menu.element()));
        expect(menu_pixels, {popup.x + 2, popup.y + 2}, {0, 0, 255, 255}, "menu overlay padding");
        expect(menu_pixels, row_probe(child.trigger()), {0, 255, 0, 255}, "selected submenu parent");
        expect(menu_pixels, row_probe(disabled.element()), {64, 64, 64, 255}, "disabled menu row");
        expect(menu_pixels, row_probe(checked.element()), {0, 0, 255, 255}, "unfocused checked row");
        const auto sep = tests::require(tree.absolute_bounds(separator.element()));
        expect(menu_pixels, {sep.x + 30, sep.y + sep.height * 0.5F}, {255, 255, 0, 255},
               "menu separator");
        const auto mark = tree.children(checked.element())[2];
        const auto arrow = tree.children(child.trigger())[3];
        if (ink(menu_pixels, mark) < 8 || ink(menu_pixels, arrow) < 12) {
            throw std::runtime_error("native menu checkmark or submenu arrow lacks visible pixels");
        }
#if TFL_TEST_GUI_TEXT
        const auto disabled_label = tests::require(
            tree.absolute_bounds(tree.children(disabled.element())[0]));
        expect(menu_pixels, {disabled_label.x + 1, disabled_label.y}, {255, 0, 255, 255},
               "disabled menu label ink");
        const auto disabled_shortcut = tests::require(
            tree.absolute_bounds(tree.children(disabled.element())[1]));
        expect(menu_pixels, {disabled_shortcut.x + 1, disabled_shortcut.y}, {255, 0, 255, 255},
               "disabled menu shortcut ink");
        const auto shortcut = tests::require(
            tree.absolute_bounds(tree.children(checked.element())[1]));
        expect(menu_pixels, {shortcut.x + 1, shortcut.y}, {0, 255, 255, 255},
               "menu shortcut display");
#endif
        const auto retained = draw_menu();
        if (retained != menu_pixels) {
            throw std::runtime_error("retained menu frame changed native pixels");
        }
        tests::require(checked.set_checked(false));
        menu_pixels = draw_menu();
        if (ink(menu_pixels, mark) != 0 || actions != 0) {
            throw std::runtime_error("clearing a checkmark left ink or invoked an action");
        }
        tests::require(tree.handle(gui::KeyEvent{.key = gui::Key::right}));
        menu_pixels = draw_menu();
        if (!child.is_open()) { throw std::runtime_error("native submenu did not open"); }
        const auto submenu = tests::require(tree.absolute_bounds(child.element()));
        expect(menu_pixels, {submenu.x + 2, submenu.y + 2}, {0, 0, 255, 255},
               "submenu escapes ancestor clip");
        expect(menu_pixels, row_probe(leaf.element()), {0, 255, 0, 255}, "submenu selected action");
        tests::require(tree.handle(gui::KeyEvent{.key = gui::Key::down}));
        tests::require(tree.handle(gui::KeyEvent{.key = gui::Key::right}));
        menu_pixels = draw_menu();
        if (!grandchild.is_open()) { throw std::runtime_error("native recursive submenu did not open"); }
        const auto recursive = tests::require(tree.absolute_bounds(grandchild.element()));
        expect(menu_pixels, {recursive.x + 2, recursive.y + 2}, {0, 0, 255, 255},
               "recursive submenu escapes both ancestor clips");
        expect(menu_pixels, row_probe(deep.element()), {0, 255, 0, 255}, "recursive selected action");
        if (ink(menu_pixels, tree.children(deep.element())[2]) < 8 || draw_menu() != menu_pixels) {
            throw std::runtime_error("recursive checkmark ink or retained submenu frame differs");
        }
        const auto disabled_point = row_probe(disabled.element());
        for (const auto action : {gui::PointerAction::down, gui::PointerAction::up}) {
            tests::require(tree.handle(gui::PointerEvent{.action = action,
                .position = disabled_point, .button = gui::PointerButton::primary}));
        }
        if (actions != 0 || !menu.is_open()) {
            throw std::runtime_error("disabled native menu action was invoked or dismissed the menu");
        }
        tests::require(tree.handle(gui::KeyEvent{.key = gui::Key::enter}));
        menu_pixels = draw_menu();
        if (actions != 1 || menu.is_open() || child.is_open() || grandchild.is_open()) {
            throw std::runtime_error("native submenu action did not dismiss exactly once");
        }
        expect(menu_pixels, {30, 55}, {0, 0, 0, 255}, "dismissed menu leaves no stale pixels");
        expect(menu_pixels, {recursive.x + 2, recursive.y + 2}, {0, 0, 0, 255},
               "dismissed recursive submenu leaves no stale pixels");
        std::cout << "Menus: exact 512x240 probes, retained-frame equality, checkmark/arrow ink, "
                     "disabled rows, recursive overlay and action dismissal";
#if TFL_TEST_GUI_TEXT
        std::cout << ", disabled glyph and shortcut colors";
#endif
        std::cout << '\n';
    }

    {
        gui::Context tree;
        const auto parent = make(tree, tree.root(), {2, 2, 8, 8}, fill({0, 0, 1, 1}));
        const auto canvas = make(tree, parent, {0, 0, 20, 20}, {});
        tests::require(tree.set_shape_builder(canvas, std::make_shared<gui::CanvasBuilder>(
            [](gui::ShapeWriter& out, gui::Size) {
                constexpr gui::Point points[] = {{0, 0}, {16, 0}, {0, 4}, {16, 4}};
                constexpr std::uint32_t indices[] = {0, 1, 2, 2, 1, 3};
                out.triangles(points, indices, {1, 0, 0, 1});
                out.line({2, 6}, {12, 6}, 2, {0, 1, 0, 1});
            })));
        const auto overlay = make(tree, parent, {6, 6, 8, 8}, fill({1, 0, 1, 1}));
        tests::require(tree.set_overlay(overlay, true));
        static_cast<void>(make(tree, tree.root(), {8, 8, 8, 8}, fill({1, 1, 0, 1})));
        const auto expected = reference({
            {{2, 2, 8, 8}, fill({0, 0, 1, 1})},
            {{2, 2, 16, 4}, fill({1, 0, 0, 1}), {2, 2, 8, 8}},
            {{3, 7, 12, 2}, fill({0, 1, 0, 1}), {2, 2, 8, 8}},
            {{8, 8, 8, 8}, fill({1, 1, 0, 1})},
            {{8, 8, 8, 8}, fill({1, 0, 1, 1})},
        });
        pixels = render_context(tree, tree.element_count());
        compare(pixels, expected, "Canvas triangles, square-ended line and unclipped overlay", 0);
        pixels = render_context(tree, 0);
        compare(pixels, expected, "Canvas streamed slot reuse without regeneration", 0);
    }
    {
        gui::Context tree;
        Decoration look = fill({1, 0, 0, 1});
        look.border = {0, 1, 0, 1};
        look.border_width = 1;
        static_cast<void>(make(tree, tree.root(), {2, 3, 4, 5}, look));
        look.border_width = 2;
        const auto expected = reference({{{4, 6, 8, 10}, look}});
        pixels = render_context(tree, tree.element_count(), 2);
        compare(pixels, expected, "Two physical pixels per logical unit", 0);
    }
    {
        // Four distinct rows/columns catch texture Y inversion as well as clip Y inversion.
        constexpr std::array<std::byte, 16> texels_2d{
            std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
            std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        auto texture_2d = tests::require(renderer::specialist::SampledTexture::upload(device, {
            .extent = {2, 2}, .bytes = texels_2d}));
        const auto texture_id = tests::require(painter.add_texture(texture_2d));
        gui::Context tree;
        Decoration look = fill({1, 1, 1, 1});
        look.texture = texture_id;
        static_cast<void>(make(tree, tree.root(), {4, 6, 8, 8}, look));
        std::vector<Pixel> expected(kSize * kSize, Pixel{0, 0, 0, 255});
        for (std::uint32_t y = 6; y < 14; ++y) {
            for (std::uint32_t x = 4; x < 12; ++x) {
                const std::size_t texel = ((y >= 10 ? 2U : 0U) + (x >= 8 ? 1U : 0U)) * 4;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    expected[y * kSize + x][channel] =
                        std::to_integer<int>(texels_2d[texel + channel]);
                }
            }
        }
        pixels = render_context(tree, tree.element_count());
        compare(pixels, expected, "Two-dimensional nearest texture orientation", 0);
        tests::require(painter.remove_texture(texture_id));
    }
    {
        constexpr std::array<std::byte, 4> translucent{
            std::byte{255}, std::byte{128}, std::byte{0}, std::byte{128}};
        auto texture = tests::require(renderer::specialist::SampledTexture::upload(device, {
            .extent = {1, 1}, .bytes = translucent}));
        const auto texture_id = tests::require(painter.add_texture(texture));
        gui::Context tree;
        tests::require(tree.set_decoration(tree.root(), fill({1, 0, 0, 1})));
        Decoration look = fill({0, 1, 1, 1});
        look.texture = texture_id;
        static_cast<void>(make(tree, tree.root(), {4, 6, 8, 8}, look));
        const auto expected_image = [](Pixel sampled) {
            std::vector<Pixel> expected(kSize * kSize, Pixel{255, 0, 0, 255});
            for (std::uint32_t y = 6; y < 14; ++y) {
                for (std::uint32_t x = 4; x < 12; ++x) {
                    expected[y * kSize + x] = sampled;
                }
            }
            return expected;
        };
        pixels = render_context(tree, tree.element_count());
        compare(pixels, expected_image({127, 64, 0, 255}),
                "Texture tint and sampled alpha premultiplied over opaque red", 0);
        constexpr std::array<std::byte, 4> updated{
            std::byte{0}, std::byte{0}, std::byte{255}, std::byte{64}};
        tests::require(texture.update(device, {0, 0, 0}, {1, 1}, updated));
        pixels = render_context(tree, 0);
        compare(pixels, expected_image({191, 0, 64, 255}),
                "Updated translucent texture with retained geometry", 0);
        tests::require(painter.remove_texture(texture_id));
    }

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
        std::cerr << "GUI drawing native test failed: " << error.what() << '\n';
        return 1;
    }
}

#include "test_support.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/draw.hpp"

#include <cmath>
#include <memory>

namespace {

using truffle::gui::CanvasBuilder;
using truffle::gui::Color;
using truffle::gui::Context;
using truffle::gui::Decoration;
using truffle::gui::DrawData;
using truffle::gui::DrawOptions;
using truffle::gui::ElementId;
using truffle::gui::ElementShapes;
using truffle::gui::LayoutParams;
using truffle::gui::Point;
using truffle::gui::Rect;
using truffle::gui::ShapeBuilder;
using truffle::gui::ShapeWriter;
using truffle::gui::Size;
using truffle::gui::TextureId;

ElementId make(Context& context, ElementId parent, Rect rect) {
    auto created = context.create(parent);
    if (!created.ok()) {
        return {};
    }
    LayoutParams params;
    params.width = rect.width;
    params.height = rect.height;
    params.position = {rect.x, rect.y};
    static_cast<void>(context.set_layout_params(created.value(), params));
    return created.value();
}

Decoration filled(Color color, TextureId texture = {}) {
    Decoration decoration;
    decoration.fill = color;
    decoration.texture = texture;
    return decoration;
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-5F;
}

void frame(Context& context, Size viewport = {100, 80}) {
    static_cast<void>(context.update_layout(viewport));
}

class TwoTextures final : public ShapeBuilder {
public:
    void build(ShapeWriter& out, Size size, const Decoration& decoration) const override {
        const Decoration image = filled({1, 1, 1, 1}, TextureId{7});
        out.rect({0, 0, size.width * 0.5F, size.height}, image);
        out.rect({size.width * 0.5F, 0, size.width * 0.5F, size.height}, image);
        out.rect({0, 0, size.width, size.height}, decoration);
    }
};

int shapes_follow_changes() {
    Context context;
    const ElementId panel = make(context, context.root(), {10, 10, 60, 40});
    const ElementId a = make(context, panel, {5, 5, 20, 10});
    const ElementId b = make(context, panel, {30, 5, 20, 10});
    TFL_CHECK(context.set_decoration(panel, filled({1, 0, 0, 1})).ok());
    TFL_CHECK(context.set_decoration(a, filled({0, 1, 0, 1})).ok());
    TFL_CHECK(context.set_decoration(b, filled({0, 0, 1, 1})).ok());

    frame(context);
    TFL_CHECK(context.update_shapes().regenerated == 4);
    TFL_CHECK(context.shapes(context.root())->empty()); // undecorated root
    TFL_CHECK(context.shapes(a)->vertices.size() == 4);

    // An idle frame regenerates nothing; neither does an unchanged decoration.
    frame(context);
    TFL_CHECK(context.update_shapes().regenerated == 0);
    TFL_CHECK(context.set_decoration(a, filled({0, 1, 0, 1})).ok());
    TFL_CHECK(context.update_shapes().regenerated == 0);

    TFL_CHECK(context.set_decoration(a, filled({1, 1, 0, 1})).ok());
    TFL_CHECK(truffle::gui::has(context.dirty(a), truffle::gui::Dirty::shape));
    TFL_CHECK(!truffle::gui::has(context.dirty(a), truffle::gui::Dirty::measure));
    TFL_CHECK(context.update_shapes().regenerated == 1);
    TFL_CHECK(context.shapes(a)->vertices[0].fill[1] == 1.0F);

    // Moving keeps element-local shapes; resizing regenerates only that element.
    LayoutParams moved = context.layout_params(b).value();
    moved.position = {32, 20};
    TFL_CHECK(context.set_layout_params(b, moved).ok());
    frame(context);
    TFL_CHECK(context.update_shapes().regenerated == 0);
    moved.width = 25.0F;
    TFL_CHECK(context.set_layout_params(b, moved).ok());
    frame(context);
    TFL_CHECK(context.update_shapes().regenerated == 1);
    TFL_CHECK(near(context.shapes(b)->vertices[0].shape[0], 12.5F));

    // A new viewport resizes only the root.
    frame(context, {120, 90});
    TFL_CHECK(context.update_shapes().regenerated == 1);

    TFL_CHECK(context.shapes(ElementId{}) == nullptr);
    TFL_CHECK(!context.set_decoration(ElementId{}, {}).ok());
    TFL_CHECK(!context.decoration(ElementId{}).ok());
    TFL_CHECK((context.decoration(b).value().fill == Color{0, 0, 1, 1}));
    return 0;
}

int rectangle_geometry() {
    ElementShapes shapes;
    ShapeWriter writer(shapes);
    Decoration look = filled({1, 0, 0, 1});
    look.border = {0, 1, 0, 1};
    look.border_width = 2;
    look.corner_radius = 3;
    writer.rect({0, 0, 20, 10}, look);
    TFL_CHECK(shapes.vertices.size() == 4);
    TFL_CHECK((shapes.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3}));
    TFL_CHECK(shapes.segments.size() == 1);
    TFL_CHECK(!shapes.segments[0].texture.valid());
    const auto& first = shapes.vertices[0];
    const auto& last = shapes.vertices[3];
    // One unit of margin around the rectangle; UVs extrapolate into it.
    TFL_CHECK(near(first.position[0], -1) && near(first.position[1], -1));
    TFL_CHECK(near(last.position[0], 21) && near(last.position[1], 11));
    TFL_CHECK(near(first.local[0], -11) && near(first.local[1], -6));
    TFL_CHECK(near(first.uv[0], -0.05F) && near(first.uv[1], -0.1F));
    TFL_CHECK(near(last.uv[0], 1.05F) && near(last.uv[1], 1.1F));
    TFL_CHECK(near(first.shape[0], 10) && near(first.shape[1], 5) &&
                  near(first.shape[2], 3) && near(first.shape[3], 2));
    TFL_CHECK(first.border[1] == 1.0F && first.fill[0] == 1.0F);

    // Radii and borders are limited to half the shorter side.
    look.corner_radius = 100;
    look.border_width = 100;
    writer.rect({0, 0, 20, 10}, look);
    TFL_CHECK(near(shapes.vertices[4].shape[2], 5) && near(shapes.vertices[4].shape[3], 5));
    TFL_CHECK(shapes.segments.size() == 1 && shapes.segments[0].index_count == 12);

    // Invisible or empty rectangles add nothing; a visible border alone draws.
    ElementShapes none;
    ShapeWriter skip(none);
    skip.rect({0, 0, 10, 10}, {});
    skip.rect({0, 0, 0, 10}, filled({1, 1, 1, 1}));
    Decoration hidden_border;
    hidden_border.border = {1, 1, 1, 1};
    skip.rect({0, 0, 10, 10}, hidden_border);
    TFL_CHECK(none.empty());
    hidden_border.border_width = 1;
    skip.rect({0, 0, 10, 10}, hidden_border);
    TFL_CHECK(none.vertices.size() == 4);

    // A custom builder: texture runs merge, a texture change starts a segment.
    Context context;
    const ElementId element = make(context, context.root(), {0, 0, 40, 20});
    TFL_CHECK(context.set_decoration(element, filled({0, 0, 1, 1})).ok());
    TFL_CHECK(context.set_shape_builder(element, std::make_shared<TwoTextures>()).ok());
    frame(context);
    static_cast<void>(context.update_shapes());
    const auto& segments = context.shapes(element)->segments;
    TFL_CHECK(segments.size() == 2);
    TFL_CHECK(segments[0].texture == TextureId{7} && segments[0].index_count == 12);
    TFL_CHECK(!segments[1].texture.valid() && segments[1].first_index == 12 &&
                  segments[1].index_count == 6);
    TFL_CHECK(context.set_shape_builder(element, nullptr).ok());
    TFL_CHECK(context.update_shapes().regenerated == 1);
    TFL_CHECK(context.shapes(element)->segments.size() == 1);
    return 0;
}

bool same_clip(const truffle::rhi::ScissorRect& clip, int x, int y, unsigned width,
               unsigned height) {
    return clip.x == x && clip.y == y && clip.width == width && clip.height == height;
}

int frame_assembly() {
    Context context;
    const ElementId panel = make(context, context.root(), {10, 10, 60, 40});
    const ElementId a = make(context, panel, {5, 5, 20, 10});
    const ElementId d = make(context, a, {15, 0, 20, 10}); // overflows a
    const ElementId c = make(context, panel, {5, 20, 10, 10});
    const ElementId b = make(context, panel, {30, 5, 20, 10});
    const ElementId e = make(context, panel, {100, 100, 10, 10}); // outside panel
    const ElementId f = make(context, e, {0, 0, 5, 5});
    for (const ElementId element : {panel, a, d, c, b, e, f}) {
        TFL_CHECK(context.set_decoration(element, filled({1, 1, 1, 1})).ok());
    }
    TFL_CHECK(context.set_decoration(b, filled({1, 1, 1, 1}, TextureId{7})).ok());
    frame(context);
    static_cast<void>(context.update_shapes());

    DrawData data;
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    // Pre-order: panel, a, d, c, b, e; f lies wholly outside the panel's clip.
    TFL_CHECK(data.elements == 6 && data.clipped == 1);
    TFL_CHECK(data.vertices.size() == 24 && data.indices.size() == 36);
    TFL_CHECK(data.commands.size() == 6);
    TFL_CHECK(same_clip(data.commands[0].clip, 0, 0, 100, 80));
    TFL_CHECK(same_clip(data.commands[1].clip, 10, 10, 60, 40));  // a
    TFL_CHECK(same_clip(data.commands[2].clip, 15, 15, 20, 10));  // d inside a
    TFL_CHECK(same_clip(data.commands[3].clip, 10, 10, 60, 40));  // c
    TFL_CHECK(data.commands[3].first_index == 18 && data.commands[3].index_count == 6);
    TFL_CHECK(data.commands[4].texture == TextureId{7});          // b
    // e matches c's texture and clip, but b lies between them in order.
    TFL_CHECK(data.commands[5].first_index == 30 && !data.commands[5].texture.valid());
    TFL_CHECK(data.indices[6] == 4 && data.indices[11] == 7); // rebased for a

    // Clip-space vertices: panel's first corner sits at (9, 9) in pixels.
    const auto& corner = data.vertices[0];
    TFL_CHECK(near(corner.position[0], -0.82F) && near(corner.position[1], 0.775F));
    TFL_CHECK(near(corner.local[0], -31) && near(corner.shape[0], 30));

    // Scale doubles pixels; clip space and command layout are unchanged.
    DrawData scaled;
    truffle::gui::assemble(context, {.target = {200, 160}, .scale = 2, .clip_y_up = false},
                           scaled);
    TFL_CHECK(near(scaled.vertices[0].position[0], -0.82F) &&
                  near(scaled.vertices[0].position[1], -0.775F));
    TFL_CHECK(near(scaled.vertices[0].local[0], -62) &&
                  near(scaled.vertices[0].shape[0], 60));
    TFL_CHECK(same_clip(scaled.commands[1].clip, 20, 20, 120, 80));

    // Without child clipping on a, d shares a's run: a, d and c merge.
    TFL_CHECK(context.set_clips_children(a, false).ok());
    TFL_CHECK(!context.clips_children(a) && context.clips_children(panel));
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    TFL_CHECK(data.commands.size() == 4);
    TFL_CHECK(data.commands[1].first_index == 6 && data.commands[1].index_count == 18);

    // Moving an element changes assembled vertices without regenerating shapes.
    LayoutParams moved = context.layout_params(panel).value();
    moved.position = {20, 10};
    TFL_CHECK(context.set_layout_params(panel, moved).ok());
    frame(context);
    TFL_CHECK(context.update_shapes().regenerated == 0);
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    TFL_CHECK(near(data.vertices[0].position[0], -0.62F));

    truffle::gui::assemble(context, {.target = {0, 80}}, data);
    TFL_CHECK(data.vertices.empty() && data.commands.empty());
    return 0;
}

int custom_drawing() {
    ElementShapes shapes;
    ShapeWriter writer(shapes);
    writer.line({0, 0}, {10, 0}, 2, {1, 0, 0, 1});
    TFL_CHECK(shapes.vertices.size() == 4 && shapes.indices.size() == 6);
    TFL_CHECK(near(shapes.vertices[0].position[0], -1) && near(shapes.vertices[0].position[1], 1));
    TFL_CHECK(near(shapes.vertices[3].position[0], 11) &&
                  near(shapes.vertices[3].position[1], -1));
    // Solid geometry sits deep inside its signed-distance shape.
    TFL_CHECK(shapes.vertices[0].local[0] == 0.0F && shapes.vertices[0].shape[0] > 1000.0F);
    const Point points[] = {{0, 0}, {4, 0}, {0, 4}};
    const std::uint32_t triangle[] = {0, 1, 2};
    const std::uint32_t broken[] = {0, 1, 3};
    writer.triangles(points, triangle, {0, 1, 0, 1});
    writer.triangles(points, broken, {0, 1, 0, 1});
    writer.triangles(points, triangle, {0, 1, 0, 0});
    writer.line({1, 1}, {1, 1}, 2, {1, 1, 1, 1});
    TFL_CHECK(shapes.vertices.size() == 7 && shapes.indices.size() == 9);
    TFL_CHECK(shapes.indices[6] == 4 && shapes.indices[8] == 6);
    TFL_CHECK(shapes.segments.size() == 1 && shapes.segments[0].index_count == 9);

    // A canvas paints after its decoration and repaints when invalidated.
    Context context;
    const ElementId canvas = make(context, context.root(), {0, 0, 40, 20});
    int paints = 0;
    Size painted;
    TFL_CHECK(context.set_decoration(canvas, filled({0, 0, 1, 1})).ok());
    TFL_CHECK(context.set_shape_builder(canvas, std::make_shared<CanvasBuilder>(
                                                    [&](ShapeWriter& out, Size size) {
                                                        ++paints;
                                                        painted = size;
                                                        out.line({0, 0}, {size.width, 0}, 1,
                                                                 {1, 1, 1, 1});
                                                    }))
                  .ok());
    frame(context);
    static_cast<void>(context.update_shapes());
    TFL_CHECK(paints == 1 && (painted == Size{40, 20}));
    TFL_CHECK(context.shapes(canvas)->indices.size() == 12);
    TFL_CHECK(context.update_shapes().regenerated == 0 && paints == 1);
    TFL_CHECK(context.invalidate(canvas, truffle::gui::Dirty::shape).ok());
    TFL_CHECK(context.update_shapes().regenerated == 1 && paints == 2);
    return 0;
}

int overlay_layers() {
    Context context;
    const ElementId panel = make(context, context.root(), {10, 10, 60, 40});
    const ElementId a = make(context, panel, {5, 5, 20, 10});
    const ElementId popup = make(context, a, {50, 50, 30, 20}); // outside panel
    const ElementId tip = make(context, popup, {2, 0, 5, 5});
    const ElementId b = make(context, panel, {30, 5, 20, 10});
    const ElementId top = make(context, context.root(), {0, 0, 10, 10});
    for (const ElementId element : {panel, a, popup, tip, b, top}) {
        TFL_CHECK(context.set_decoration(element, filled({1, 1, 1, 1})).ok());
    }
    TFL_CHECK(context.set_clips_children(panel, true).ok());
    for (const ElementId element : {popup, tip, top}) {
        TFL_CHECK(context.set_overlay(element, true).ok());
        TFL_CHECK(context.overlay(element));
    }
    TFL_CHECK(!context.overlay(a));
    TFL_CHECK(!context.set_overlay(context.root(), true).ok());
    frame(context);
    static_cast<void>(context.update_shapes());

    DrawData data;
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    // Normal tree first (panel, a, b), then overlays in tree order with only
    // the viewport as clip: popup, the nested tip, then top.
    TFL_CHECK(data.elements == 6 && data.clipped == 0);
    TFL_CHECK(data.commands.size() == 3);
    TFL_CHECK(same_clip(data.commands[1].clip, 10, 10, 60, 40) &&
                  data.commands[1].index_count == 12);
    TFL_CHECK(same_clip(data.commands[2].clip, 0, 0, 100, 80) &&
                  data.commands[2].index_count == 18);
    TFL_CHECK(near(data.vertices[12].position[0], 0.28F)); // popup at 64 px
    TFL_CHECK(near(data.vertices[16].position[0], 0.32F)); // tip at 66 px
    TFL_CHECK(near(data.vertices[20].position[0], -1.02F)); // top at -1 px

    // Scrolling the container still moves its overlays.
    TFL_CHECK(context.set_child_offset(panel, {-10, 0}).ok());
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    TFL_CHECK(near(data.vertices[12].position[0], 0.08F));

    TFL_CHECK(context.set_overlay(popup, false).ok());
    TFL_CHECK(context.destroy(top).ok());
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    // popup is drawn in the normal tree again; its nested tip stays on top.
    TFL_CHECK(data.elements == 5 && data.commands.back().index_count == 6);
    return 0;
}

} // namespace

int main() {
    if (custom_drawing() != 0) {
        return 1;
    }
    if (overlay_layers() != 0) {
        return 1;
    }
    if (shapes_follow_changes() != 0) {
        return 1;
    }
    if (rectangle_geometry() != 0) {
        return 1;
    }
    return frame_assembly();
}

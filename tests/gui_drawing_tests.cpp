#include "test_support.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/draw.hpp"

#include <cmath>
#include <memory>

namespace {

using truffle::gui::Color;
using truffle::gui::Context;
using truffle::gui::Decoration;
using truffle::gui::DrawData;
using truffle::gui::DrawOptions;
using truffle::gui::ElementId;
using truffle::gui::ElementShapes;
using truffle::gui::LayoutParams;
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
    TRUFFLE_CHECK(context.set_decoration(panel, filled({1, 0, 0, 1})).ok());
    TRUFFLE_CHECK(context.set_decoration(a, filled({0, 1, 0, 1})).ok());
    TRUFFLE_CHECK(context.set_decoration(b, filled({0, 0, 1, 1})).ok());

    frame(context);
    TRUFFLE_CHECK(context.update_shapes().regenerated == 4);
    TRUFFLE_CHECK(context.shapes(context.root())->empty()); // undecorated root
    TRUFFLE_CHECK(context.shapes(a)->vertices.size() == 4);

    // An idle frame regenerates nothing; neither does an unchanged decoration.
    frame(context);
    TRUFFLE_CHECK(context.update_shapes().regenerated == 0);
    TRUFFLE_CHECK(context.set_decoration(a, filled({0, 1, 0, 1})).ok());
    TRUFFLE_CHECK(context.update_shapes().regenerated == 0);

    TRUFFLE_CHECK(context.set_decoration(a, filled({1, 1, 0, 1})).ok());
    TRUFFLE_CHECK(truffle::gui::has(context.dirty(a), truffle::gui::Dirty::shape));
    TRUFFLE_CHECK(!truffle::gui::has(context.dirty(a), truffle::gui::Dirty::measure));
    TRUFFLE_CHECK(context.update_shapes().regenerated == 1);
    TRUFFLE_CHECK(context.shapes(a)->vertices[0].fill[1] == 1.0F);

    // Moving keeps element-local shapes; resizing regenerates only that element.
    LayoutParams moved = context.layout_params(b).value();
    moved.position = {32, 20};
    TRUFFLE_CHECK(context.set_layout_params(b, moved).ok());
    frame(context);
    TRUFFLE_CHECK(context.update_shapes().regenerated == 0);
    moved.width = 25;
    TRUFFLE_CHECK(context.set_layout_params(b, moved).ok());
    frame(context);
    TRUFFLE_CHECK(context.update_shapes().regenerated == 1);
    TRUFFLE_CHECK(near(context.shapes(b)->vertices[0].shape[0], 12.5F));

    // A new viewport resizes only the root.
    frame(context, {120, 90});
    TRUFFLE_CHECK(context.update_shapes().regenerated == 1);

    TRUFFLE_CHECK(context.shapes(ElementId{}) == nullptr);
    TRUFFLE_CHECK(!context.set_decoration(ElementId{}, {}).ok());
    TRUFFLE_CHECK(!context.decoration(ElementId{}).ok());
    TRUFFLE_CHECK((context.decoration(b).value().fill == Color{0, 0, 1, 1}));
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
    TRUFFLE_CHECK(shapes.vertices.size() == 4);
    TRUFFLE_CHECK((shapes.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3}));
    TRUFFLE_CHECK(shapes.segments.size() == 1);
    TRUFFLE_CHECK(!shapes.segments[0].texture.valid());
    const auto& first = shapes.vertices[0];
    const auto& last = shapes.vertices[3];
    // One unit of margin around the rectangle; UVs extrapolate into it.
    TRUFFLE_CHECK(near(first.position[0], -1) && near(first.position[1], -1));
    TRUFFLE_CHECK(near(last.position[0], 21) && near(last.position[1], 11));
    TRUFFLE_CHECK(near(first.local[0], -11) && near(first.local[1], -6));
    TRUFFLE_CHECK(near(first.uv[0], -0.05F) && near(first.uv[1], -0.1F));
    TRUFFLE_CHECK(near(last.uv[0], 1.05F) && near(last.uv[1], 1.1F));
    TRUFFLE_CHECK(near(first.shape[0], 10) && near(first.shape[1], 5) &&
                  near(first.shape[2], 3) && near(first.shape[3], 2));
    TRUFFLE_CHECK(first.border[1] == 1.0F && first.fill[0] == 1.0F);

    // Radii and borders are limited to half the shorter side.
    look.corner_radius = 100;
    look.border_width = 100;
    writer.rect({0, 0, 20, 10}, look);
    TRUFFLE_CHECK(near(shapes.vertices[4].shape[2], 5) && near(shapes.vertices[4].shape[3], 5));
    TRUFFLE_CHECK(shapes.segments.size() == 1 && shapes.segments[0].index_count == 12);

    // Invisible or empty rectangles add nothing; a visible border alone draws.
    ElementShapes none;
    ShapeWriter skip(none);
    skip.rect({0, 0, 10, 10}, {});
    skip.rect({0, 0, 0, 10}, filled({1, 1, 1, 1}));
    Decoration hidden_border;
    hidden_border.border = {1, 1, 1, 1};
    skip.rect({0, 0, 10, 10}, hidden_border);
    TRUFFLE_CHECK(none.empty());
    hidden_border.border_width = 1;
    skip.rect({0, 0, 10, 10}, hidden_border);
    TRUFFLE_CHECK(none.vertices.size() == 4);

    // A custom builder: texture runs merge, a texture change starts a segment.
    Context context;
    const ElementId element = make(context, context.root(), {0, 0, 40, 20});
    TRUFFLE_CHECK(context.set_decoration(element, filled({0, 0, 1, 1})).ok());
    TRUFFLE_CHECK(context.set_shape_builder(element, std::make_shared<TwoTextures>()).ok());
    frame(context);
    static_cast<void>(context.update_shapes());
    const auto& segments = context.shapes(element)->segments;
    TRUFFLE_CHECK(segments.size() == 2);
    TRUFFLE_CHECK(segments[0].texture == TextureId{7} && segments[0].index_count == 12);
    TRUFFLE_CHECK(!segments[1].texture.valid() && segments[1].first_index == 12 &&
                  segments[1].index_count == 6);
    TRUFFLE_CHECK(context.set_shape_builder(element, nullptr).ok());
    TRUFFLE_CHECK(context.update_shapes().regenerated == 1);
    TRUFFLE_CHECK(context.shapes(element)->segments.size() == 1);
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
        TRUFFLE_CHECK(context.set_decoration(element, filled({1, 1, 1, 1})).ok());
    }
    TRUFFLE_CHECK(context.set_decoration(b, filled({1, 1, 1, 1}, TextureId{7})).ok());
    frame(context);
    static_cast<void>(context.update_shapes());

    DrawData data;
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    // Pre-order: panel, a, d, c, b, e; f lies wholly outside the panel's clip.
    TRUFFLE_CHECK(data.elements == 6 && data.clipped == 1);
    TRUFFLE_CHECK(data.vertices.size() == 24 && data.indices.size() == 36);
    TRUFFLE_CHECK(data.commands.size() == 6);
    TRUFFLE_CHECK(same_clip(data.commands[0].clip, 0, 0, 100, 80));
    TRUFFLE_CHECK(same_clip(data.commands[1].clip, 10, 10, 60, 40));  // a
    TRUFFLE_CHECK(same_clip(data.commands[2].clip, 15, 15, 20, 10));  // d inside a
    TRUFFLE_CHECK(same_clip(data.commands[3].clip, 10, 10, 60, 40));  // c
    TRUFFLE_CHECK(data.commands[3].first_index == 18 && data.commands[3].index_count == 6);
    TRUFFLE_CHECK(data.commands[4].texture == TextureId{7});          // b
    // e matches c's texture and clip, but b lies between them in order.
    TRUFFLE_CHECK(data.commands[5].first_index == 30 && !data.commands[5].texture.valid());
    TRUFFLE_CHECK(data.indices[6] == 4 && data.indices[11] == 7); // rebased for a

    // Clip-space vertices: panel's first corner sits at (9, 9) in pixels.
    const auto& corner = data.vertices[0];
    TRUFFLE_CHECK(near(corner.position[0], -0.82F) && near(corner.position[1], 0.775F));
    TRUFFLE_CHECK(near(corner.local[0], -31) && near(corner.shape[0], 30));

    // Scale doubles pixels; clip space and command layout are unchanged.
    DrawData scaled;
    truffle::gui::assemble(context, {.target = {200, 160}, .scale = 2, .clip_y_up = false},
                           scaled);
    TRUFFLE_CHECK(near(scaled.vertices[0].position[0], -0.82F) &&
                  near(scaled.vertices[0].position[1], -0.775F));
    TRUFFLE_CHECK(near(scaled.vertices[0].local[0], -62) &&
                  near(scaled.vertices[0].shape[0], 60));
    TRUFFLE_CHECK(same_clip(scaled.commands[1].clip, 20, 20, 120, 80));

    // Without child clipping on a, d shares a's run: a, d and c merge.
    TRUFFLE_CHECK(context.set_clips_children(a, false).ok());
    TRUFFLE_CHECK(!context.clips_children(a) && context.clips_children(panel));
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    TRUFFLE_CHECK(data.commands.size() == 4);
    TRUFFLE_CHECK(data.commands[1].first_index == 6 && data.commands[1].index_count == 18);

    // Moving an element changes assembled vertices without regenerating shapes.
    LayoutParams moved = context.layout_params(panel).value();
    moved.position = {20, 10};
    TRUFFLE_CHECK(context.set_layout_params(panel, moved).ok());
    frame(context);
    TRUFFLE_CHECK(context.update_shapes().regenerated == 0);
    truffle::gui::assemble(context, {.target = {100, 80}}, data);
    TRUFFLE_CHECK(near(data.vertices[0].position[0], -0.62F));

    truffle::gui::assemble(context, {.target = {0, 80}}, data);
    TRUFFLE_CHECK(data.vertices.empty() && data.commands.empty());
    return 0;
}

} // namespace

int main() {
    if (shapes_follow_changes() != 0) {
        return 1;
    }
    if (rectangle_geometry() != 0) {
        return 1;
    }
    return frame_assembly();
}

#include "truffle/gui/painter.hpp"

#include "truffle/gui/context.hpp"

#include <vector>

namespace truffle::gui {

base::Result<ThemeValue> Painter::property(Property property) const {
    return context_->property(element_, property);
}

void Painter::rect(const Rect& rect, const Decoration& decoration) {
    writer_->rect(rect, decoration);
}

void Painter::rect(const Rect& rect, Color fill, float corner_radius) {
    writer_->rect(rect, {.fill = fill, .corner_radius = corner_radius});
}

void Painter::frame(const Rect& rect, float width, Color color, float corner_radius) {
    writer_->rect(rect, {.border = color, .border_width = width,
                         .corner_radius = corner_radius});
}

void Painter::circle(Point center, float radius, Color fill) {
    if (radius <= 0.0F) { return; }
    writer_->rect({center.x - radius, center.y - radius, 2 * radius, 2 * radius},
                  {.fill = fill, .corner_radius = radius});
}

void Painter::ring(Point center, float radius, float width, Color color) {
    if (radius <= 0.0F) { return; }
    writer_->rect({center.x - radius, center.y - radius, 2 * radius, 2 * radius},
                  {.border = color, .border_width = width, .corner_radius = radius});
}

void Painter::line(Point from, Point to, float width, Color color) {
    writer_->line(from, to, width, color);
}

void Painter::polyline(std::span<const Point> points, float width, Color color,
                       bool closed) {
    for (std::size_t i = 1; i < points.size(); ++i) {
        writer_->line(points[i - 1], points[i], width, color);
    }
    if (closed && points.size() > 2) {
        writer_->line(points.back(), points.front(), width, color);
    }
}

void Painter::triangles(std::span<const Point> points, std::span<const std::uint32_t> indices,
                        Color color) {
    writer_->triangles(points, indices, color);
}

void Painter::image(const Rect& rect, TextureId texture, Rect uv, Color tint) {
    writer_->rect(rect, {.fill = tint, .texture = texture, .uv = uv});
}

void Painter::text(Point origin, const TextLayout& layout, TextureId atlas, Color color) {
    for (const TextGlyph& glyph : layout.glyphs) {
        if (glyph.size.width <= 0.0F || glyph.size.height <= 0.0F) { continue; }
        writer_->rect({origin.x + glyph.position.x, origin.y + glyph.position.y,
                       glyph.size.width, glyph.size.height},
                      {.fill = color, .texture = atlas, .uv = glyph.uv});
    }
}

} // namespace truffle::gui

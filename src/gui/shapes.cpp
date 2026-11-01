#include "truffle/gui/shapes.hpp"

#include <algorithm>
#include <cmath>

namespace truffle::gui {
namespace {

// Quads extend past the rectangle so anti-aliased edges are not cut off.
constexpr float edge_margin = 1.0F;

[[nodiscard]] bool visible(const Decoration& decoration) noexcept {
    return decoration.fill.a > 0.0F ||
           (decoration.border.a > 0.0F && decoration.border_width > 0.0F);
}

} // namespace

void ShapeWriter::rect(const Rect& rect, const Decoration& decoration) {
    if (rect.width <= 0.0F || rect.height <= 0.0F || !visible(decoration)) {
        return;
    }
    const float half_width = rect.width * 0.5F;
    const float half_height = rect.height * 0.5F;
    const float radius = std::clamp(decoration.corner_radius, 0.0F,
                                    std::min(half_width, half_height));
    const float border = std::clamp(decoration.border_width, 0.0F,
                                    std::min(half_width, half_height));
    const float centre_x = rect.x + half_width;
    const float centre_y = rect.y + half_height;
    const Rect& uv = decoration.uv;
    const Color& fill = decoration.fill;
    const Color& edge = decoration.border;

    auto& vertices = shapes_->vertices;
    const auto base = static_cast<std::uint32_t>(vertices.size());
    for (const auto [sx, sy] : {std::pair{-1.0F, -1.0F}, std::pair{1.0F, -1.0F},
                                std::pair{-1.0F, 1.0F}, std::pair{1.0F, 1.0F}}) {
        const float local_x = sx * (half_width + edge_margin);
        const float local_y = sy * (half_height + edge_margin);
        // UVs extrapolate linearly into the margin so the inside maps exactly.
        const float u = uv.x + (local_x + half_width) / rect.width * uv.width;
        const float v = uv.y + (local_y + half_height) / rect.height * uv.height;
        vertices.push_back({
            {centre_x + local_x, centre_y + local_y},
            {u, v},
            {fill.r, fill.g, fill.b, fill.a},
            {edge.r, edge.g, edge.b, edge.a},
            {local_x, local_y},
            {half_width, half_height, radius, border},
        });
    }

    for (const std::uint32_t corner : {0U, 1U, 2U, 2U, 1U, 3U}) {
        shapes_->indices.push_back(base + corner);
    }
    append_indices(decoration.texture, 6);
}

void ShapeWriter::triangles(std::span<const Point> points,
                            std::span<const std::uint32_t> indices, Color color) {
    const std::size_t count = indices.size() - indices.size() % 3;
    if (count == 0 || color.a <= 0.0F ||
        std::any_of(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(count),
                    [&](std::uint32_t index) { return index >= points.size(); })) {
        return;
    }
    // A zero offset inside a huge rectangle keeps the signed distance fully
    // inside, so the fragment stage passes the fill through unchanged.
    constexpr float inside = 1.0e6F;
    auto& vertices = shapes_->vertices;
    const auto base = static_cast<std::uint32_t>(vertices.size());
    for (const Point& point : points) {
        vertices.push_back({
            {point.x, point.y},
            {0.5F, 0.5F},
            {color.r, color.g, color.b, color.a},
            {0.0F, 0.0F, 0.0F, 0.0F},
            {0.0F, 0.0F},
            {inside, inside, 0.0F, 0.0F},
        });
    }
    for (std::size_t i = 0; i < count; ++i) {
        shapes_->indices.push_back(base + indices[i]);
    }
    append_indices({}, static_cast<std::uint32_t>(count));
}

void ShapeWriter::line(Point from, Point to, float width, Color color) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    if (length <= 0.0F || width <= 0.0F) {
        return;
    }
    const float half = width * 0.5F;
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    const float ex = dx / length * half;
    const float ey = dy / length * half;
    const Point corners[] = {{from.x - ex + nx, from.y - ey + ny},
                             {from.x - ex - nx, from.y - ey - ny},
                             {to.x + ex + nx, to.y + ey + ny},
                             {to.x + ex - nx, to.y + ey - ny}};
    constexpr std::uint32_t quad[] = {0, 1, 2, 2, 1, 3};
    triangles(corners, quad, color);
}

void ShapeWriter::append_indices(TextureId texture, std::uint32_t count) {
    auto& segments = shapes_->segments;
    if (!segments.empty() && segments.back().texture == texture) {
        segments.back().index_count += count;
    } else {
        segments.push_back(
            {texture, static_cast<std::uint32_t>(shapes_->indices.size()) - count, count});
    }
}

void DecorationBuilder::build(ShapeWriter& out, Size size,
                              const Decoration& decoration) const {
    out.rect({0.0F, 0.0F, size.width, size.height}, decoration);
}

void CanvasBuilder::build(ShapeWriter& out, Size size, const Decoration& decoration) const {
    out.rect({0.0F, 0.0F, size.width, size.height}, decoration);
    if (paint_) {
        paint_(out, size);
    }
}

} // namespace truffle::gui

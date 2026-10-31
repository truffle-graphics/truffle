#include "truffle/gui/shapes.hpp"

#include <algorithm>

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

    auto& indices = shapes_->indices;
    const auto first = static_cast<std::uint32_t>(indices.size());
    for (const std::uint32_t corner : {0U, 1U, 2U, 2U, 1U, 3U}) {
        indices.push_back(base + corner);
    }
    auto& segments = shapes_->segments;
    if (!segments.empty() && segments.back().texture == decoration.texture) {
        segments.back().index_count += 6;
    } else {
        segments.push_back({decoration.texture, first, 6});
    }
}

void DecorationBuilder::build(ShapeWriter& out, Size size,
                              const Decoration& decoration) const {
    out.rect({0.0F, 0.0F, size.width, size.height}, decoration);
}

} // namespace truffle::gui

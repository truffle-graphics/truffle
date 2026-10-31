#include "truffle/gui/draw.hpp"

#include <algorithm>
#include <cmath>

namespace truffle::gui {
namespace {

struct Bounds {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;

    [[nodiscard]] bool empty() const noexcept {
        return right <= left || bottom <= top;
    }
    [[nodiscard]] Bounds intersect(const Bounds& other) const noexcept {
        return {std::max(left, other.left), std::max(top, other.top),
                std::min(right, other.right), std::min(bottom, other.bottom)};
    }
    [[nodiscard]] rhi::ScissorRect scissor() const noexcept {
        return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                static_cast<std::uint32_t>(right - left),
                static_cast<std::uint32_t>(bottom - top)};
    }
    friend bool operator==(const Bounds&, const Bounds&) = default;
};

struct Pending {
    ElementId element;
    Point origin; // parent's absolute origin, layout units
    Bounds clip;
};

} // namespace

void assemble(const Context& context, const DrawOptions& options, DrawData& out) {
    out.vertices.clear();
    out.indices.clear();
    out.commands.clear();
    out.elements = 0;
    out.clipped = 0;
    if (options.target.width == 0 || options.target.height == 0 || options.scale <= 0.0F) {
        return;
    }
    const float scale = options.scale;
    const float to_clip_x = 2.0F / static_cast<float>(options.target.width);
    const float to_clip_y = 2.0F / static_cast<float>(options.target.height);
    const float y_sign = options.clip_y_up ? -1.0F : 1.0F;
    const auto pixel = [scale](float units) { return std::lround(units * scale); };

    Bounds last_clip;
    std::vector<Pending> pending{{context.root(), {}, {0, 0,
                                  static_cast<long>(options.target.width),
                                  static_cast<long>(options.target.height)}}};
    while (!pending.empty()) {
        const Pending item = pending.back();
        pending.pop_back();
        if (item.clip.empty()) {
            ++out.clipped;
            continue;
        }
        const auto bounds = context.bounds(item.element);
        const Rect rect = bounds.ok() ? bounds.value() : Rect{};
        const Point origin{item.origin.x + rect.x, item.origin.y + rect.y};

        const ElementShapes* shapes = context.shapes(item.element);
        if (shapes != nullptr && !shapes->empty()) {
            ++out.elements;
            const auto base = static_cast<std::uint32_t>(out.vertices.size());
            for (ShapeVertex vertex : shapes->vertices) {
                const float x = (origin.x + vertex.position[0]) * scale;
                const float y = (origin.y + vertex.position[1]) * scale;
                vertex.position[0] = x * to_clip_x - 1.0F;
                vertex.position[1] = y_sign * (y * to_clip_y - 1.0F);
                vertex.local[0] *= scale;
                vertex.local[1] *= scale;
                for (float& value : vertex.shape) {
                    value *= scale;
                }
                out.vertices.push_back(vertex);
            }
            for (const ShapeSegment& segment : shapes->segments) {
                const auto first = static_cast<std::uint32_t>(out.indices.size());
                for (std::uint32_t i = 0; i < segment.index_count; ++i) {
                    out.indices.push_back(base + shapes->indices[segment.first_index + i]);
                }
                if (!out.commands.empty() && out.commands.back().texture == segment.texture &&
                    last_clip == item.clip) {
                    out.commands.back().index_count += segment.index_count;
                } else {
                    out.commands.push_back(
                        {segment.texture, item.clip.scissor(), first, segment.index_count});
                    last_clip = item.clip;
                }
            }
        }

        Bounds child_clip = item.clip;
        if (context.clips_children(item.element)) {
            child_clip = child_clip.intersect({pixel(origin.x), pixel(origin.y),
                                               pixel(origin.x + rect.width),
                                               pixel(origin.y + rect.height)});
        }
        const auto children = context.children(item.element);
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            pending.push_back({*child, origin, child_clip});
        }
    }
}

} // namespace truffle::gui

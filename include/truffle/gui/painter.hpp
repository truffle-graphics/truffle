#pragma once

#include "truffle/base/status.hpp"
#include "truffle/gui/element.hpp"
#include "truffle/gui/shapes.hpp"
#include "truffle/gui/text.hpp"
#include "truffle/gui/theme.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace truffle::gui {

class Context;

// Where custom painting goes relative to an element's own look.
enum class PaintLayer : std::uint8_t {
    background, // under the element's decoration and text
    foreground, // over the element's decoration and text
    replace,    // instead of the element's decoration; text is still drawn
};

// Writes custom shapes for one element, in element-local layout units.
// Painting is retained: a paint function runs only when the element's shapes
// are regenerated (new, resized, restyled or invalidated) and its output is
// reused for every frame until then. Painters are created by the Context and
// are valid only during the paint function.
class Painter {
public:
    Painter(const Context& context, ElementId element, ShapeWriter& writer, Size size,
            const Decoration& decoration) noexcept
        : context_(&context), element_(element), writer_(&writer), size_(size),
          decoration_(decoration) {}

    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] Rect bounds() const noexcept { return {0, 0, size_.width, size_.height}; }
    // The element's resolved look (fill, border, radius), from its style.
    [[nodiscard]] const Decoration& decoration() const noexcept { return decoration_; }
    // The element's resolved style property, including overrides.
    [[nodiscard]] base::Result<ThemeValue> property(Property property) const;
    [[nodiscard]] ElementId element() const noexcept { return element_; }

    void rect(const Rect& rect, const Decoration& decoration);
    void rect(const Rect& rect, Color fill, float corner_radius = 0.0F);
    // A rectangle outline drawn inside `rect`.
    void frame(const Rect& rect, float width, Color color, float corner_radius = 0.0F);
    void circle(Point center, float radius, Color fill);
    void ring(Point center, float radius, float width, Color color);
    void line(Point from, Point to, float width, Color color);
    void polyline(std::span<const Point> points, float width, Color color, bool closed = false);
    void triangles(std::span<const Point> points, std::span<const std::uint32_t> indices,
                   Color color);
    void image(const Rect& rect, TextureId texture, Rect uv = {0, 0, 1, 1},
               Color tint = {1, 1, 1, 1});
    // Shaped text with its glyph atlas, with `origin` at the layout's top-left.
    void text(Point origin, const TextLayout& layout, TextureId atlas, Color color);

    // Lower-level access to the element's shape stream.
    [[nodiscard]] ShapeWriter& writer() noexcept { return *writer_; }

private:
    const Context* context_;
    ElementId element_;
    ShapeWriter* writer_;
    Size size_;
    Decoration decoration_;
};

using PaintFunction = std::function<void(Painter&)>;

} // namespace truffle::gui

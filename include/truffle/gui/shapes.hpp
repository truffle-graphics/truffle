#pragma once

#include "truffle/gui/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace truffle::gui {

// Names a texture registered with a Painter. The default value means "none":
// shapes then sample an opaque white texel, so only their colors show.
struct TextureId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend bool operator==(const TextureId&, const TextureId&) = default;
};

// How one rectangle looks. Colors are straight alpha. A texture is sampled
// over `uv` and multiplies `fill`; the border is drawn inside the rectangle.
struct Decoration {
    Color fill;
    Color border;
    float border_width = 0.0F;
    float corner_radius = 0.0F;
    TextureId texture;
    Rect uv{0.0F, 0.0F, 1.0F, 1.0F};

    friend bool operator==(const Decoration&, const Decoration&) = default;
};

// GPU vertex of the GUI program (72 bytes). In stored shapes `position` is in
// element-local layout units; frame assembly rewrites it to clip space and
// scales `local` and `shape` to target pixels.
struct ShapeVertex {
    float position[2];
    float uv[2];
    float fill[4];   // straight alpha
    float border[4]; // straight alpha
    float local[2];  // offset from the rectangle centre
    float shape[4];  // half width, half height, corner radius, border width
};
static_assert(sizeof(ShapeVertex) == 72);

// A run of indices drawn with one texture.
struct ShapeSegment {
    TextureId texture;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

// Generated drawing data of one element, kept until the element changes.
struct ElementShapes {
    std::vector<ShapeVertex> vertices;
    std::vector<std::uint32_t> indices; // relative to `vertices`
    std::vector<ShapeSegment> segments; // in drawing order

    [[nodiscard]] bool empty() const noexcept {
        return indices.empty();
    }
};

// Appends rectangles to an element's shapes, in drawing order.
class ShapeWriter {
public:
    explicit ShapeWriter(ElementShapes& shapes) noexcept : shapes_(&shapes) {}

    // `rect` is in element-local layout units. Invisible or empty rectangles
    // add nothing.
    void rect(const Rect& rect, const Decoration& decoration);

private:
    ElementShapes* shapes_;
};

// Generates an element's shapes from its size and decoration. Set one per
// element to draw more than its background rectangle; builders are shared
// and must not keep per-element state.
class ShapeBuilder {
public:
    virtual ~ShapeBuilder() = default;
    virtual void build(ShapeWriter& out, Size size, const Decoration& decoration) const = 0;
};

// Default builder: the decoration over the element's whole bounds.
class DecorationBuilder final : public ShapeBuilder {
public:
    void build(ShapeWriter& out, Size size, const Decoration& decoration) const override;
};

struct ShapeStats {
    std::size_t regenerated = 0;

    friend bool operator==(const ShapeStats&, const ShapeStats&) = default;
};

} // namespace truffle::gui

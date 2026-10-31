#pragma once

#include "truffle/gui/element.hpp"
#include "truffle/gui/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace truffle::gui {

class Context;

enum class Axis : std::uint8_t { horizontal, vertical };

enum class Align : std::uint8_t { start, center, end, stretch };

// Sizing and placement inputs of one element. The context applies the fixed
// size, minimum, maximum and padding; layouts only place children.
struct LayoutParams {
    std::optional<float> width;  // fixed width; content width when empty
    std::optional<float> height; // fixed height; content height when empty
    Size min;
    Size max{unbounded, unbounded};
    Edges padding;
    // Share of leftover main-axis space inside a StackLayout. Zero keeps the
    // measured size; a fixed main-axis size always wins over a weight.
    float weight = 0.0F;
    Point position; // offset inside a FreeLayout parent's content area

    friend bool operator==(const LayoutParams&, const LayoutParams&) = default;
};

// Work done by one `Context::update_layout` call. Cache hits are not counted.
struct LayoutStats {
    std::size_t measured = 0;
    std::size_t arranged = 0;
};

// Access a layout has to the tree while it runs. Created by the context only.
class LayoutPass {
public:
    LayoutPass(const LayoutPass&) = delete;
    LayoutPass& operator=(const LayoutPass&) = delete;

    [[nodiscard]] std::span<const ElementId> children(ElementId element) const noexcept;
    [[nodiscard]] const LayoutParams& params(ElementId element) const noexcept;

    // Desired size of `child` under `constraints`. Reused while the child is
    // unchanged and the constraints are equal.
    [[nodiscard]] Size measure(ElementId child, Constraints constraints);

    // Size returned by the child's most recent measure.
    [[nodiscard]] Size desired(ElementId child) const noexcept;

    // Places `child` at `rect`, in the coordinates of its parent. Its subtree
    // is arranged again only if its size changed or it was invalidated.
    void arrange(ElementId child, Rect rect);

private:
    friend class Context;
    explicit LayoutPass(Context& context) noexcept : context_(context) {}

    Context& context_;
};

// Places an element's children: measure under constraints, then arrange.
// Implementations must be immutable once attached; replace the layout object
// to change its configuration so the context can invalidate it.
class Layout {
public:
    virtual ~Layout() = default;

    // Size the children need, excluding the element's padding. `content`
    // already has the padding removed.
    [[nodiscard]] virtual Size measure(LayoutPass& pass, ElementId element,
                                       Constraints content) const = 0;

    // Places children inside `content`, in the element's own coordinates.
    virtual void arrange(LayoutPass& pass, ElementId element, Rect content) const = 0;
};

struct StackOptions {
    Axis axis = Axis::vertical;
    float spacing = 0.0F;
    // Placement of the group when space is left over; `stretch` acts as
    // `start` on the main axis (use weights to fill it).
    Align main = Align::start;
    Align cross = Align::stretch;
};

// Row (horizontal) or column (vertical). Weighted children share leftover
// main-axis space within their minimum and maximum sizes, growing or shrinking.
class StackLayout final : public Layout {
public:
    explicit StackLayout(StackOptions options = {}) noexcept;

    [[nodiscard]] const StackOptions& options() const noexcept;

    [[nodiscard]] Size measure(LayoutPass& pass, ElementId element,
                               Constraints content) const override;
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override;

private:
    StackOptions options_;
};

// Children keep their measured size at `LayoutParams::position`. This is the
// default layout of every element.
class FreeLayout final : public Layout {
public:
    [[nodiscard]] Size measure(LayoutPass& pass, ElementId element,
                               Constraints content) const override;
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override;
};

} // namespace truffle::gui

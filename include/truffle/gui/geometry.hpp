#pragma once

#include <algorithm>
#include <limits>

namespace truffle::gui {

// Geometry is expressed in layout units. Stack layouts snap edges to whole
// units so neighbouring elements never leave gaps or overlap.
inline constexpr float unbounded = std::numeric_limits<float>::infinity();

struct Size {
    float width = 0.0F;
    float height = 0.0F;

    friend bool operator==(const Size&, const Size&) = default;
};

struct Point {
    float x = 0.0F;
    float y = 0.0F;

    friend bool operator==(const Point&, const Point&) = default;
};

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] constexpr Size size() const noexcept {
        return {width, height};
    }

    friend bool operator==(const Rect&, const Rect&) = default;
};

struct Edges {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] constexpr float horizontal() const noexcept {
        return left + right;
    }
    [[nodiscard]] constexpr float vertical() const noexcept {
        return top + bottom;
    }

    friend bool operator==(const Edges&, const Edges&) = default;
};

// Straight-alpha color with channels in [0, 1], in the render target's encoding.
struct Color {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;

    friend bool operator==(const Color&, const Color&) = default;
};

// Range of sizes a parent accepts from a child. `max` may be `unbounded`.
struct Constraints {
    Size min;
    Size max{unbounded, unbounded};

    [[nodiscard]] static constexpr Constraints tight(Size size) noexcept {
        return {size, size};
    }

    [[nodiscard]] constexpr Size clamp(Size size) const noexcept {
        return {std::clamp(size.width, min.width, std::max(min.width, max.width)),
                std::clamp(size.height, min.height, std::max(min.height, max.height))};
    }

    friend bool operator==(const Constraints&, const Constraints&) = default;
};

} // namespace truffle::gui

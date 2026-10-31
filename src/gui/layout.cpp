#include "truffle/gui/layout.hpp"

#include "truffle/gui/context.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace truffle::gui {
namespace {

[[nodiscard]] float main_of(Axis axis, Size size) noexcept {
    return axis == Axis::horizontal ? size.width : size.height;
}

[[nodiscard]] float cross_of(Axis axis, Size size) noexcept {
    return axis == Axis::horizontal ? size.height : size.width;
}

[[nodiscard]] Size along(Axis axis, float main, float cross) noexcept {
    return axis == Axis::horizontal ? Size{main, cross} : Size{cross, main};
}

[[nodiscard]] const std::optional<float>& fixed_main(Axis axis,
                                                     const LayoutParams& params) noexcept {
    return axis == Axis::horizontal ? params.width : params.height;
}

[[nodiscard]] const std::optional<float>& fixed_cross(Axis axis,
                                                      const LayoutParams& params) noexcept {
    return axis == Axis::horizontal ? params.height : params.width;
}

[[nodiscard]] float aligned_offset(Align align, float free) noexcept {
    switch (align) {
    case Align::center:
        return free / 2.0F;
    case Align::end:
        return free;
    case Align::start:
    case Align::stretch:
        break;
    }
    return 0.0F;
}

} // namespace

StackLayout::StackLayout(StackOptions options) noexcept : options_(options) {}

const StackOptions& StackLayout::options() const noexcept {
    return options_;
}

Size StackLayout::measure(LayoutPass& pass, ElementId element,
                          Constraints content) const {
    const Axis axis = options_.axis;
    const Constraints child_constraints{
        {}, along(axis, unbounded, cross_of(axis, content.max))};
    float main = 0.0F;
    float cross = 0.0F;
    const auto children = pass.children(element);
    for (const ElementId child : children) {
        const Size size = pass.measure(child, child_constraints);
        main += main_of(axis, size);
        cross = std::max(cross, cross_of(axis, size));
    }
    if (children.size() > 1) {
        main += options_.spacing * static_cast<float>(children.size() - 1);
    }
    return along(axis, main, cross);
}

void StackLayout::arrange(LayoutPass& pass, ElementId element, Rect content) const {
    const Axis axis = options_.axis;
    const auto children = pass.children(element);
    const std::size_t count = children.size();
    if (count == 0) {
        return;
    }

    std::vector<float> sizes(count, 0.0F);
    std::vector<bool> flexible(count, false);
    float used = options_.spacing * static_cast<float>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        const LayoutParams& params = pass.params(children[i]);
        if (params.weight > 0.0F && !fixed_main(axis, params)) {
            flexible[i] = true;
        } else {
            sizes[i] = main_of(axis, pass.desired(children[i]));
            used += sizes[i];
        }
    }

    // Distribute the remaining space by weight; children that would leave
    // their minimum or maximum are frozen there and the rest is shared again.
    float remaining = main_of(axis, content.size()) - used;
    std::vector<bool> open = flexible;
    for (;;) {
        float total_weight = 0.0F;
        for (std::size_t i = 0; i < count; ++i) {
            if (open[i]) {
                total_weight += pass.params(children[i]).weight;
            }
        }
        if (total_weight <= 0.0F) {
            break;
        }
        bool froze = false;
        float frozen_total = 0.0F;
        for (std::size_t i = 0; i < count; ++i) {
            if (!open[i]) {
                continue;
            }
            const LayoutParams& params = pass.params(children[i]);
            const float low = std::max(0.0F, main_of(axis, params.min));
            const float high = std::max(low, main_of(axis, params.max));
            const float share = remaining * params.weight / total_weight;
            if (share < low || share > high) {
                sizes[i] = share < low ? low : high;
                open[i] = false;
                frozen_total += sizes[i];
                froze = true;
            } else {
                sizes[i] = share;
            }
        }
        if (!froze) {
            break;
        }
        remaining -= frozen_total;
    }

    float total = options_.spacing * static_cast<float>(count - 1);
    for (const float size : sizes) {
        total += size;
    }
    const float free = main_of(axis, content.size()) - total;
    const float main_origin = axis == Axis::horizontal ? content.x : content.y;
    const float cross_origin = axis == Axis::horizontal ? content.y : content.x;
    const float cross_space = cross_of(axis, content.size());

    float cursor = main_origin + (free > 0.0F ? aligned_offset(options_.main, free) : 0.0F);
    for (std::size_t i = 0; i < count; ++i) {
        const ElementId child = children[i];
        const LayoutParams& params = pass.params(child);
        float cross = cross_of(axis, pass.desired(child));
        if (options_.cross == Align::stretch && !fixed_cross(axis, params)) {
            const float low = cross_of(axis, params.min);
            cross = std::clamp(cross_space, low, std::max(low, cross_of(axis, params.max)));
        }
        const float cross_start = cross_origin + aligned_offset(options_.cross, cross_space - cross);

        const float m0 = std::round(cursor);
        const float m1 = std::round(cursor + sizes[i]);
        const float c0 = std::round(cross_start);
        const float c1 = std::round(cross_start + cross);
        cursor += sizes[i] + options_.spacing;

        pass.arrange(child, axis == Axis::horizontal ? Rect{m0, c0, m1 - m0, c1 - c0}
                                                     : Rect{c0, m0, c1 - c0, m1 - m0});
    }
}

Size FreeLayout::measure(LayoutPass& pass, ElementId element, Constraints) const {
    Size extent;
    for (const ElementId child : pass.children(element)) {
        const Size size = pass.measure(child, {});
        const Point position = pass.params(child).position;
        extent.width = std::max(extent.width, position.x + size.width);
        extent.height = std::max(extent.height, position.y + size.height);
    }
    return extent;
}

void FreeLayout::arrange(LayoutPass& pass, ElementId element, Rect content) const {
    for (const ElementId child : pass.children(element)) {
        const Size size = pass.desired(child);
        const Point position = pass.params(child).position;
        pass.arrange(child, {content.x + position.x, content.y + position.y, size.width,
                             size.height});
    }
}

std::span<const ElementId> LayoutPass::children(ElementId element) const noexcept {
    return context_.children(element);
}

const LayoutParams& LayoutPass::params(ElementId element) const noexcept {
    return context_.params_of(element);
}

Size LayoutPass::measure(ElementId child, Constraints constraints) {
    return context_.measure_element(child, constraints);
}

Size LayoutPass::desired(ElementId child) const noexcept {
    return context_.desired_of(child);
}

void LayoutPass::arrange(ElementId child, Rect rect) {
    context_.arrange_element(child, rect);
}

} // namespace truffle::gui

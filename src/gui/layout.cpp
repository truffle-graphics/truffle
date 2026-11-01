#include "truffle/gui/layout.hpp"

#include "truffle/gui/context.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace truffle::gui {

Point LayoutPass::child_offset(ElementId element) const noexcept {
    return context_.child_offset(element);
}

void LayoutPass::set_child_offset(ElementId element, Point offset) {
    static_cast<void>(context_.set_child_offset(element, offset));
}
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

struct CellSpan {
    std::size_t start = 0;
    std::size_t count = 1;
    float size = 0.0F;
};

[[nodiscard]] std::size_t track_count(const std::vector<Track>& declared,
                                      const std::vector<CellSpan>& spans) noexcept {
    std::size_t count = declared.size();
    for (const CellSpan& span : spans) {
        count = std::max(count, span.start + span.count);
    }
    return count;
}

[[nodiscard]] Track track_at(const std::vector<Track>& declared, std::size_t index) noexcept {
    return index < declared.size() ? declared[index] : Track::content();
}

[[nodiscard]] float clamp_track(const Track& track, float size) noexcept {
    const float low = std::max(0.0F, track.min);
    return std::clamp(size, low, std::max(low, track.max));
}

// Fixed tracks take their value; content and weighted tracks fit their children.
[[nodiscard]] std::vector<float> natural_tracks(const std::vector<Track>& declared,
                                                const std::vector<CellSpan>& spans,
                                                float spacing) {
    const std::size_t count = track_count(declared, spans);
    std::vector<float> sizes(count, 0.0F);
    for (std::size_t i = 0; i < count; ++i) {
        const Track track = track_at(declared, i);
        if (track.sizing == TrackSizing::fixed) {
            sizes[i] = std::max(0.0F, track.value);
        }
    }
    for (const CellSpan& span : spans) {
        if (span.count == 1 && track_at(declared, span.start).sizing != TrackSizing::fixed) {
            sizes[span.start] = std::max(sizes[span.start], span.size);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        sizes[i] = clamp_track(track_at(declared, i), sizes[i]);
    }
    for (const CellSpan& span : spans) {
        if (span.count < 2) {
            continue;
        }
        float covered = spacing * static_cast<float>(span.count - 1);
        std::size_t growable = 0;
        for (std::size_t i = span.start; i < span.start + span.count; ++i) {
            covered += sizes[i];
            growable += track_at(declared, i).sizing == TrackSizing::fixed ? 0U : 1U;
        }
        if (span.size <= covered || growable == 0) {
            continue;
        }
        const float share = (span.size - covered) / static_cast<float>(growable);
        for (std::size_t i = span.start; i < span.start + span.count; ++i) {
            const Track track = track_at(declared, i);
            if (track.sizing != TrackSizing::fixed) {
                sizes[i] = clamp_track(track, sizes[i] + share);
            }
        }
    }
    return sizes;
}

[[nodiscard]] float track_extent(const std::vector<float>& sizes, float spacing) noexcept {
    float total = sizes.empty() ? 0.0F : spacing * static_cast<float>(sizes.size() - 1);
    for (const float size : sizes) {
        total += size;
    }
    return total;
}

// Weighted tracks share what the other tracks leave of `space`; tracks that
// would leave their minimum or maximum are frozen there and the rest share again.
void share_weighted(const std::vector<Track>& declared, std::vector<float>& sizes,
                    float space, float spacing) {
    std::vector<bool> open(sizes.size(), false);
    float remaining = space - (sizes.empty() ? 0.0F
                                             : spacing * static_cast<float>(sizes.size() - 1));
    bool any_open = false;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const Track track = track_at(declared, i);
        if (track.sizing == TrackSizing::weight && track.value > 0.0F) {
            open[i] = true;
            any_open = true;
        } else {
            remaining -= sizes[i];
        }
    }
    while (any_open) {
        float total_weight = 0.0F;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            total_weight += open[i] ? track_at(declared, i).value : 0.0F;
        }
        if (total_weight <= 0.0F) {
            break;
        }
        bool froze = false;
        float frozen_total = 0.0F;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            if (!open[i]) {
                continue;
            }
            const Track track = track_at(declared, i);
            const float share = std::max(0.0F, remaining) * track.value / total_weight;
            const float clamped = clamp_track(track, share);
            sizes[i] = clamped;
            if (clamped != share) {
                open[i] = false;
                frozen_total += clamped;
                froze = true;
            }
        }
        if (!froze) {
            break;
        }
        remaining -= frozen_total;
    }
}

[[nodiscard]] std::vector<float> track_starts(const std::vector<float>& sizes, float origin,
                                              float spacing) {
    std::vector<float> starts(sizes.size(), origin);
    float cursor = origin;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        starts[i] = cursor;
        cursor += sizes[i] + spacing;
    }
    return starts;
}

// Edges of a child inside spanned cells, before rounding.
[[nodiscard]] std::pair<float, float> place_in_cell(Align align, float start, float cell,
                                                    float desired,
                                                    const std::optional<float>& fixed,
                                                    float min, float max) noexcept {
    float size = desired;
    if (align == Align::stretch && !fixed) {
        size = std::clamp(cell, min, std::max(min, max));
    }
    const float begin = start + aligned_offset(align, cell - size);
    return {begin, begin + size};
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

GridLayout::GridLayout(GridOptions options) : options_(std::move(options)) {}

const GridOptions& GridLayout::options() const noexcept {
    return options_;
}

Size GridLayout::measure(LayoutPass& pass, ElementId element, Constraints) const {
    std::vector<CellSpan> columns;
    std::vector<CellSpan> rows;
    const auto fixed_span = [](const std::vector<Track>& declared, std::size_t start,
                               std::size_t count, float spacing) {
        float total = spacing * static_cast<float>(count - 1);
        for (std::size_t i = start; i < start + count; ++i) {
            const Track track = track_at(declared, i);
            if (track.sizing != TrackSizing::fixed) {
                return unbounded;
            }
            total += clamp_track(track, track.value);
        }
        return total;
    };
    for (const ElementId child : pass.children(element)) {
        const GridCell& cell = pass.params(child).cell;
        const std::size_t column_span = std::max<std::size_t>(1, cell.column_span);
        const std::size_t row_span = std::max<std::size_t>(1, cell.row_span);
        const Size size = pass.measure(
            child, {{},
                    {fixed_span(options_.columns, cell.column, column_span,
                                options_.column_spacing),
                     fixed_span(options_.rows, cell.row, row_span, options_.row_spacing)}});
        columns.push_back({cell.column, column_span, size.width});
        rows.push_back({cell.row, row_span, size.height});
    }
    return {track_extent(natural_tracks(options_.columns, columns, options_.column_spacing),
                         options_.column_spacing),
            track_extent(natural_tracks(options_.rows, rows, options_.row_spacing),
                         options_.row_spacing)};
}

void GridLayout::arrange(LayoutPass& pass, ElementId element, Rect content) const {
    const auto children = pass.children(element);
    std::vector<CellSpan> columns;
    std::vector<CellSpan> rows;
    for (const ElementId child : children) {
        const GridCell& cell = pass.params(child).cell;
        const Size size = pass.desired(child);
        columns.push_back({cell.column, std::max<std::size_t>(1, cell.column_span), size.width});
        rows.push_back({cell.row, std::max<std::size_t>(1, cell.row_span), size.height});
    }
    std::vector<float> widths =
        natural_tracks(options_.columns, columns, options_.column_spacing);
    std::vector<float> heights = natural_tracks(options_.rows, rows, options_.row_spacing);
    share_weighted(options_.columns, widths, content.width, options_.column_spacing);
    share_weighted(options_.rows, heights, content.height, options_.row_spacing);
    const auto xs = track_starts(widths, content.x, options_.column_spacing);
    const auto ys = track_starts(heights, content.y, options_.row_spacing);

    for (std::size_t i = 0; i < children.size(); ++i) {
        const ElementId child = children[i];
        const LayoutParams& params = pass.params(child);
        const Size desired = pass.desired(child);
        const std::size_t column_end = columns[i].start + columns[i].count;
        const std::size_t row_end = rows[i].start + rows[i].count;
        const float cell_x = xs[columns[i].start];
        const float cell_y = ys[rows[i].start];
        const float cell_width = xs[column_end - 1] + widths[column_end - 1] - cell_x;
        const float cell_height = ys[row_end - 1] + heights[row_end - 1] - cell_y;
        const auto [x0, x1] = place_in_cell(params.cell.horizontal, cell_x, cell_width,
                                            desired.width, params.width, params.min.width,
                                            params.max.width);
        const auto [y0, y1] = place_in_cell(params.cell.vertical, cell_y, cell_height,
                                            desired.height, params.height, params.min.height,
                                            params.max.height);
        const float left = std::round(x0);
        const float top = std::round(y0);
        pass.arrange(child, {left, top, std::round(x1) - left, std::round(y1) - top});
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

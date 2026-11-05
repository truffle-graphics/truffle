#include "truffle/gui/text.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace truffle::gui {

std::size_t TextLayout::hit_test(Point point) const noexcept {
    const TextCaret* best = nullptr;
    double best_line = std::numeric_limits<double>::infinity();
    double best_x = std::numeric_limits<double>::infinity();
    for (const auto& caret : carets) {
        const double line_distance = std::abs(static_cast<double>(point.y) -
            (static_cast<double>(caret.position.y) + static_cast<double>(caret.height) * 0.5));
        const double x_distance = std::abs(static_cast<double>(point.x) - caret.position.x);
        if (best == nullptr || line_distance < best_line ||
            (line_distance == best_line &&
             (caret.position.y > best->position.y ||
              (caret.position.y == best->position.y && x_distance < best_x)))) {
            best = &caret;
            best_line = line_distance;
            best_x = x_distance;
        }
    }
    return best == nullptr ? 0 : best->byte;
}

Point TextLayout::caret_position(std::size_t byte) const noexcept {
    const TextCaret* best = nullptr;
    auto distance = std::numeric_limits<std::size_t>::max();
    for (const auto& caret : carets) {
        const auto candidate = byte > caret.byte ? byte - caret.byte : caret.byte - byte;
        if (best == nullptr || candidate < distance) {
            best = &caret;
            distance = candidate;
        }
        if (candidate == 0) {
            break;
        }
    }
    return best == nullptr ? Point{} : best->position;
}

std::vector<Rect> TextLayout::selection_rects(std::size_t begin, std::size_t end) const {
    if (begin > end) {
        std::swap(begin, end);
    }
    std::vector<Rect> rectangles;
    if (begin == end) {
        return rectangles;
    }
    const TextCluster* previous = nullptr;
    for (const auto& cluster : clusters) {
        if (cluster.begin >= end || cluster.end <= begin || cluster.bounds.width <= 0.0F) {
            previous = nullptr;
            continue;
        }
        if (previous != nullptr && previous->right_to_left == cluster.right_to_left &&
            previous->bounds.y == cluster.bounds.y &&
            previous->bounds.height == cluster.bounds.height &&
            std::abs(rectangles.back().x + rectangles.back().width - cluster.bounds.x) < 0.001F &&
            (previous->end == cluster.begin || cluster.end == previous->begin)) {
            rectangles.back().width = cluster.bounds.x + cluster.bounds.width - rectangles.back().x;
        } else {
            rectangles.push_back(cluster.bounds);
        }
        previous = &cluster;
    }
    return rectangles;
}

} // namespace truffle::gui

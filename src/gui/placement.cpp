#include "truffle/gui/context.hpp"

#include <algorithm>
#include <cmath>

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status invalid_element() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI element handle is not alive");
}

} // namespace

std::span<const ElementId> Context::flow_children(ElementId element) const noexcept {
    const Node* node = find(element);
    if (node == nullptr) {
        return {};
    }
    return node->placed_children ? std::span<const ElementId>(node->flow)
                                 : std::span<const ElementId>(node->children);
}

void Context::refresh_flow(ElementId parent) {
    Node& node = *find(parent);
    node.flow.clear();
    node.placed_children = false;
    for (const ElementId child : node.children) {
        const Node& child_node = *find(child);
        if (child_node.placement || child_node.popup || !child_node.visible) {
            node.placed_children = true;
        } else {
            node.flow.push_back(child);
        }
    }
    if (!node.placed_children) {
        node.flow = {};
    }
}

base::Status Context::set_placement(ElementId element, std::optional<Rect> rect) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (element == root_) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "the GUI root element cannot be placed");
    }
    if (rect && (node->popup || !std::isfinite(rect->x) || !std::isfinite(rect->y) ||
                 std::isnan(rect->width) || std::isnan(rect->height) || rect->width < 0.0F ||
                 rect->height < 0.0F)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "a GUI placement needs a non-negative size and no popup");
    }
    if (node->placement == rect) {
        return base::Status::success();
    }
    const bool same_size = node->placement && rect && node->placement->size() == rect->size();
    const bool toggled = node->placement.has_value() != rect.has_value();
    node->placement = rect;
    if (toggled) {
        refresh_flow(node->parent);
    }
    if (same_size) {
        Node& parent = *find(node->parent);
        raise(node->parent, parent, Dirty::arrange);
    } else {
        mark(element, Dirty::measure);
    }
    return base::Status::success();
}

std::optional<Rect> Context::placement(ElementId element) const noexcept {
    const Node* node = find(element);
    return node == nullptr ? std::nullopt : node->placement;
}

base::Status Context::set_overlay(ElementId element, bool overlay) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_element();
    }
    if (element == root_) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "the GUI root element cannot be an overlay");
    }
    if (node->overlay != overlay) {
        node->overlay = overlay;
        overlays_ = overlay ? overlays_ + 1 : overlays_ - 1;
        frame_requested_ = true;
    }
    return base::Status::success();
}

bool Context::overlay(ElementId element) const noexcept {
    const Node* node = find(element);
    return node != nullptr && node->overlay;
}

bool Context::visible(ElementId element) const noexcept {
    for (auto up = element; up.valid();) {
        const Node* node = find(up);
        if (node == nullptr || !node->visible) { return false; }
        up = node->parent;
    }
    return element.valid();
}

base::Status Context::set_visible(ElementId element, bool value) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    if (!node->parent.valid()) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "a GUI root cannot be collapsed");
    }
    if (node->visible == value) { return base::Status::success(); }
    node->visible = value;
    if (find(node->parent) != nullptr) { refresh_flow(node->parent); }
    mark(element, Dirty::measure);
    if (value) {
        std::vector<ElementId> pending{element};
        while (!pending.empty()) {
            const ElementId current = pending.back();
            pending.pop_back();
            mark(current, Dirty::measure | Dirty::arrange | Dirty::shape);
            const auto descendants = children(current);
            pending.insert(pending.end(), descendants.begin(), descendants.end());
        }
    }
    request_frame();
    return set_interaction(element, node->interaction);
}

base::Status Context::set_popup(ElementId element, std::optional<PopupSide> side) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_element(); }
    if (!node->parent.valid() || (side && (node->placement || *side > PopupSide::right))) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "a GUI popup needs a parent, a valid side and no placement");
    }
    if (node->popup == side) { return base::Status::success(); }
    const bool had_popup = node->popup.has_value();
    node->popup = side;
    if (side && !had_popup) { popups_.push_back(element); }
    else if (!side) { std::erase(popups_, element); }
    refresh_flow(node->parent);
    mark(element, Dirty::measure);
    return set_overlay(element, side.has_value());
}

} // namespace truffle::gui

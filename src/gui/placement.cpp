#include "truffle/gui/context.hpp"

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
        if (find(child)->placement) {
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
    if (rect && (rect->width < 0.0F || rect->height < 0.0F)) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "a GUI placement needs a non-negative size");
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

} // namespace truffle::gui

#include "truffle/gui/context.hpp"
#include "truffle/gui/surface.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {

bool contains(Rect rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

Rect intersect(Rect a, Rect b) {
    const float x = std::max(a.x, b.x);
    const float y = std::max(a.y, b.y);
    return {x, y, std::max(0.0F, std::min(a.x + a.width, b.x + b.width) - x),
            std::max(0.0F, std::min(a.y + a.height, b.y + b.height) - y)};
}

base::Status invalid_target() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI input target is not an eligible live element");
}

} // namespace

base::Status Context::set_interaction(ElementId element, Interaction interaction) {
    if (auto* surface = surface_of(element); surface != nullptr && root_ != surface->root()) {
        return surface->update([this, element, interaction] {
            return set_interaction(element, interaction);
        });
    }
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_target();
    }
    node->interaction = interaction;
    if (node->component && node->component->state.disabled == interaction.enabled) {
        auto state = node->component->state;
        state.disabled = !interaction.enabled;
        const auto status = set_component_state(element, state);
        if (!status.ok()) { return status; }
    }
    if (focused_.valid() &&
        (!input_enabled(focused_) || !find(focused_)->interaction.focusable)) {
        const auto status = focus({});
        if (!status.ok()) { return status; }
    }
    std::vector<std::uint32_t> release;
    for (const auto& [pointer, target] : captures_) {
        if (!input_enabled(target)) {
            release.push_back(pointer);
        }
    }
    for (auto pointer : release) {
        const auto status = release_pointer(pointer);
        if (!status.ok()) { return status; }
    }
    if (!input_enabled(hovered_)) {
        update_hover({});
    }
    return base::Status::success();
}

base::Result<Interaction> Context::interaction(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_target();
    }
    return node->interaction;
}

base::Status Context::set_event_handler(ElementId element, EventHandler handler) {
    Node* node = find(element);
    if (node == nullptr) {
        return invalid_target();
    }
    node->handler = std::move(handler);
    return base::Status::success();
}

base::Result<EventHandler> Context::event_handler(ElementId element) const {
    const Node* node = find(element);
    if (node == nullptr) {
        return invalid_target();
    }
    return node->handler;
}

bool Context::input_enabled(ElementId element) const noexcept {
    for (ElementId up = element; up.valid();) {
        const Node* node = find(up);
        if (node == nullptr || !node->visible || !node->interaction.enabled) {
            return false;
        }
        if (up == root_) { return true; }
        up = node->parent;
    }
    return false;
}

ElementId Context::hit_test(Point position) const {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return {};
    }
    const Node* root = find(root_);
    if (!root->arranged || !contains(root->rect, position)) {
        return {};
    }
    struct Entry { ElementId id; Point origin; Rect clip; bool self; };
    const Rect viewport = root->rect;
    // Searches one layer, topmost first; overlays below it are separate layers.
    const auto search = [&](const Entry& start) -> ElementId {
        std::vector<Entry> pending{start};
        while (!pending.empty()) {
            const Entry entry = pending.back();
            pending.pop_back();
            const Node* node = find(entry.id);
            if (node == nullptr || !node->visible || !node->interaction.enabled || !node->arranged ||
                !contains(entry.clip, position)) {
                continue;
            }
            const Rect rect{entry.origin.x + node->rect.x, entry.origin.y + node->rect.y,
                            node->rect.width, node->rect.height};
            if (entry.self) {
                if (node->interaction.hit_test && contains(rect, position)) {
                    return entry.id;
                }
                continue;
            }
            pending.push_back({entry.id, entry.origin, entry.clip, true});
            const Rect clip = node->clips ? intersect(entry.clip, rect) : entry.clip;
            // The last drawn child is visited first.
            for (ElementId child : node->children) {
                if (find(child)->overlay) {
                    continue;
                }
                pending.push_back({child, {rect.x + node->child_offset.x,
                                          rect.y + node->child_offset.y}, clip, false});
            }
        }
        return {};
    };
    const Entry base{root_, {}, viewport, false};
    if (overlays_ == 0) {
        return search(base);
    }

    // Gather layers in drawing order (see draw.cpp), skipping disabled subtrees.
    std::vector<Entry> layers{base};
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        std::vector<Entry> nested;
        std::vector<Entry> pending{layers[layer]};
        while (!pending.empty()) {
            const Entry entry = pending.back();
            pending.pop_back();
            const Node* node = find(entry.id);
            if (node == nullptr || !node->visible || !node->interaction.enabled || !node->arranged) {
                continue;
            }
            if (entry.self) { // an overlay reached in tree order
                nested.push_back({entry.id, entry.origin, viewport, false});
                continue;
            }
            const Point origin{entry.origin.x + node->rect.x + node->child_offset.x,
                               entry.origin.y + node->rect.y + node->child_offset.y};
            for (auto child = node->children.rbegin(); child != node->children.rend();
                 ++child) {
                pending.push_back({*child, origin, viewport, find(*child)->overlay});
            }
        }
        layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(layer) + 1,
                      nested.begin(), nested.end());
    }
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        if (const ElementId hit = search(*layer); hit.valid()) {
            return hit;
        }
    }
    return {};
}

base::Result<bool> Context::dispatch(ElementId target, RoutedPayload payload, bool bubble) {
    std::vector<ElementId> path;
    for (ElementId up = target; up.valid();) {
        const Node* node = find(up);
        if (node == nullptr) {
            break;
        }
        path.push_back(up);
        up = bubble ? node->parent : ElementId{};
    }
    RoutedEvent event{target, {}, std::move(payload)};
    for (ElementId current : path) {
        const Node* node = find(current);
        if (node == nullptr ||
            (std::holds_alternative<InputEvent>(event.payload) && !input_enabled(current))) {
            continue;
        }
        event.current = current;
        // Callbacks may destroy/reparent elements, replace themselves, or grow nodes_.
        const EventHandler handler = node->handler;
        if (handler) {
            handler(*this, event);
        }
        const EventTrace trace = trace_;
        if (trace) {
            trace(event);
        }
        if (!event.status.ok()) {
            event_status_ = event.status;
            break;
        }
        if (event.stopped) {
            break;
        }
    }
    if (!event.status.ok()) { return event.status; }
    return event.handled;
}

ElementId Context::hovered() const noexcept { return hovered_; }
ElementId Context::focused() const noexcept { return focused_; }

void Context::update_hover(ElementId next) {
    if (next == hovered_) {
        return;
    }
    const auto path = [this](ElementId id) {
        std::vector<ElementId> result;
        while (const Node* node = find(id)) {
            result.push_back(id);
            id = node->parent;
        }
        return result;
    };
    const auto old_path = path(hovered_);
    const auto new_path = path(next);
    hovered_ = next;
    for (ElementId id : old_path) {
        if (std::find(new_path.begin(), new_path.end(), id) == new_path.end()) {
            static_cast<void>(dispatch(id, HoverEvent{false}, false));
        }
    }
    for (auto it = new_path.rbegin(); it != new_path.rend(); ++it) {
        if (std::find(old_path.begin(), old_path.end(), *it) == old_path.end()) {
            static_cast<void>(dispatch(*it, HoverEvent{true}, false));
        }
    }
}

base::Status Context::focus(ElementId element) {
    if (element.valid() && (!input_.focused || !input_enabled(element) ||
                            !find(element)->interaction.focusable)) {
        return invalid_target();
    }
    if (focused_ == element) {
        return base::Status::success();
    }
    const ElementId previous = focused_;
    focused_ = element;
    input_.composition = {};
    const auto lost = dispatch(previous, ElementFocusEvent{false}, false);
    if (!lost.ok()) { return lost.status(); }
    if (focused_ == element) {
        const auto gained = dispatch(element, ElementFocusEvent{true}, false);
        if (!gained.ok()) { return gained.status(); }
    }
    return base::Status::success();
}

base::Status Context::focus_next(bool backwards) {
    std::vector<ElementId> order;
    std::vector<ElementId> pending{root_};
    while (!pending.empty()) {
        const ElementId id = pending.back();
        pending.pop_back();
        const Node* node = find(id);
        if (node == nullptr || !node->visible || !node->interaction.enabled) {
            continue;
        }
        if (node->interaction.focusable) {
            order.push_back(id);
        }
        pending.insert(pending.end(), node->children.rbegin(), node->children.rend());
    }
    if (order.empty()) {
        return focus({});
    }
    auto found = std::find(order.begin(), order.end(), focused_);
    std::size_t index = backwards ? order.size() - 1 : 0;
    if (found != order.end()) {
        const auto current = static_cast<std::size_t>(found - order.begin());
        index = backwards ? (current + order.size() - 1) % order.size()
                          : (current + 1) % order.size();
    }
    return focus(order[index]);
}

base::Status Context::capture_pointer(ElementId element, std::uint32_t pointer) {
    if (!input_enabled(element) || !input_.focused) {
        return invalid_target();
    }
    if (captured(pointer) == element) {
        return base::Status::success();
    }
    const ElementId previous = captured(pointer);
    captures_[pointer] = element;
    const auto lost = dispatch(previous, CaptureLostEvent{pointer}, false);
    return lost.ok() ? base::Status::success() : lost.status();
}

base::Status Context::release_pointer(std::uint32_t pointer) {
    const ElementId previous = captured(pointer);
    captures_.erase(pointer);
    const auto lost = dispatch(previous, CaptureLostEvent{pointer}, false);
    return lost.ok() ? base::Status::success() : lost.status();
}

ElementId Context::captured(std::uint32_t pointer) const noexcept {
    const auto found = captures_.find(pointer);
    return found == captures_.end() ? ElementId{} : found->second;
}

void Context::forget_input(ElementId element) noexcept {
    if (hovered_ == element) { hovered_ = {}; }
    if (focused_ == element) {
        focused_ = {};
        input_.composition = {};
        host().set_text_input_area(std::nullopt);
        host().set_text_input_state(std::nullopt);
    }
    std::erase_if(captures_, [element](const auto& entry) { return entry.second == element; });
}

void Context::set_event_trace(EventTrace trace) { trace_ = std::move(trace); }
const base::Status& Context::last_event_status() const noexcept { return event_status_; }

base::Status Context::set_input_filter(ElementId element, EventHandler filter) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_target(); }
    const bool had_filter = static_cast<bool>(node->filter);
    node->filter = std::move(filter);
    if (node->filter && !had_filter) { input_filters_.push_back(element); }
    else if (!node->filter) { std::erase(input_filters_, element); }
    return base::Status::success();
}

base::Result<bool> Context::filter_input(const InputEvent& input) {
    ElementId target = focused_;
    if (const auto* pointer = std::get_if<PointerEvent>(&input)) {
        target = hit_test(pointer->position);
    } else if (const auto* wheel = std::get_if<WheelEvent>(&input)) {
        target = hit_test(wheel->position);
    }
    RoutedEvent event{target, {}, input};
    const auto filters = input_filters_;
    for (const ElementId element : filters) {
        const Node* node = find(element);
        if (node == nullptr || !input_enabled(element)) { continue; }
        auto up = element;
        while (const Node* ancestor = find(up)) {
            if (!ancestor->parent.valid()) { break; }
            up = ancestor->parent;
        }
        if (up != root_) { continue; }
        const EventHandler filter = node->filter;
        if (!filter) { continue; }
        event.current = element;
        filter(*this, event);
        if (!event.status.ok()) { return event.status; }
        if (event.stopped) { break; }
    }
    return event.handled;
}

void Context::route_input(const InputEvent& event, bool filtered) {
    if (const auto* pointer = std::get_if<PointerEvent>(&event)) {
        const bool outside = pointer->action == PointerAction::leave ||
                             pointer->action == PointerAction::cancel;
        update_hover(outside ? ElementId{} : hit_test(pointer->position));
        ElementId target = captured(pointer->pointer);
        if (!target.valid()) { target = hovered_; }
        if (pointer->action == PointerAction::down) {
            pointer_buttons_[pointer->pointer] |=
                static_cast<std::uint8_t>(1U << static_cast<unsigned>(pointer->button));
            ElementId candidate = target;
            while (const Node* node = find(candidate)) {
                if (node->interaction.focusable) { break; }
                candidate = node->parent;
            }
            if (input_.focused && !filtered) { static_cast<void>(focus(candidate)); }
        }
        if (!filtered) { static_cast<void>(dispatch(target, event)); }
        if (pointer->action == PointerAction::up) {
            auto found = pointer_buttons_.find(pointer->pointer);
            if (found != pointer_buttons_.end()) {
                found->second &= static_cast<std::uint8_t>(
                    ~(1U << static_cast<unsigned>(pointer->button)));
                if (found->second == 0) { pointer_buttons_.erase(found); }
            }
            if (!pointer_buttons_.contains(pointer->pointer)) {
                release_pointer(pointer->pointer);
            }
        } else if (pointer->action == PointerAction::cancel) {
            pointer_buttons_.erase(pointer->pointer);
            release_pointer(pointer->pointer);
        }
    } else if (const auto* wheel = std::get_if<WheelEvent>(&event)) {
        update_hover(hit_test(wheel->position));
        if (!filtered) { static_cast<void>(dispatch(hovered_, event)); }
    } else if (const auto* focus_event = std::get_if<FocusEvent>(&event)) {
        if (!filtered) { static_cast<void>(dispatch(focused_, event)); }
        if (!focus_event->focused) {
            static_cast<void>(focus({}));
            std::vector<std::uint32_t> pointers;
            for (const auto& [pointer_id, target] : captures_) {
                static_cast<void>(target);
                pointers.push_back(pointer_id);
            }
            for (auto pointer_id : pointers) { release_pointer(pointer_id); }
            pointer_buttons_.clear();
            update_hover({});
        }
    } else if (input_.focused && !filtered) {
        const auto result = dispatch(focused_, event);
        const bool handled = !result.ok() || result.value();
        if (const auto* key = std::get_if<KeyEvent>(&event);
            key != nullptr && key->key == Key::tab && key->down && !handled) {
            focus_next(has(key->modifiers, Modifiers::shift));
        }
    }
}

} // namespace truffle::gui

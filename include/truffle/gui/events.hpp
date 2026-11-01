#pragma once

#include "truffle/gui/element.hpp"
#include "truffle/gui/input.hpp"

#include <functional>
#include <variant>

namespace truffle::gui {

class Context;

struct Interaction {
    bool hit_test = true; // false skips this element, not its children
    bool enabled = true;  // false excludes the entire subtree from input
    bool focusable = false;
    friend bool operator==(const Interaction&, const Interaction&) = default;
};

struct HoverEvent { bool entered = false; };
struct ElementFocusEvent { bool focused = false; };
struct CaptureLostEvent { std::uint32_t pointer = 0; };
using RoutedPayload =
    std::variant<InputEvent, HoverEvent, ElementFocusEvent, CaptureLostEvent>;

struct RoutedEvent {
    ElementId target;
    ElementId current;
    RoutedPayload payload;
    bool stopped = false;
    bool handled = false;

    void stop_propagation() noexcept { stopped = true; }
    void prevent_default() noexcept { handled = true; }

    template <typename T>
    [[nodiscard]] const T* input() const noexcept {
        const auto* event = std::get_if<InputEvent>(&payload);
        return event == nullptr ? nullptr : std::get_if<T>(event);
    }
};

using EventHandler = std::function<void(Context&, RoutedEvent&)>;
using EventTrace = std::function<void(const RoutedEvent&)>;

} // namespace truffle::gui

#pragma once

#include "truffle/gui/geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace truffle::gui {

enum class Cursor : std::uint8_t {
    arrow,
    text,
    hand,
    crosshair,
    move,
    resize_horizontal,
    resize_vertical,
    not_allowed,
    hidden,
};

// Platform services the GUI asks of its host. The host owns windows, the event
// loop and scheduling; this interface is how the GUI reaches the platform.
// Every default does nothing, so a host implements only what it supports.
class Host {
public:
    Host() = default;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    virtual ~Host() = default;

    virtual void set_cursor(Cursor /*cursor*/) {}

    // UTF-8 clipboard text; empty when unavailable.
    [[nodiscard]] virtual std::string clipboard_text() { return {}; }
    virtual void set_clipboard_text(std::string_view /*text*/) {}

    // Where an input method should place its candidate window, in logical
    // root coordinates; empty when no text input is active.
    virtual void set_text_input_area(std::optional<Rect> /*area*/) {}
};

} // namespace truffle::gui

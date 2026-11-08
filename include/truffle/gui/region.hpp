#pragma once

#include "truffle/gui/context.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace truffle::gui {

class RegionScope;

struct RegionOptions {
    bool live = false;    // rerun every frame instead of only when invalidated
    float spacing = 0.0F; // between the region's top-level items (a column)
    TextStyle text;       // text engine/font/atlas for labels, buttons, checkboxes
};

// A subtree whose contents are described in immediate style. The body reruns in
// `Context::prepare_frame` after creation, `invalidate`, a user action on one of
// its widgets, or every frame while live. Items are matched to persistent
// widgets by id (unique within their container), so unchanged items keep their
// elements and shapes; items not described in a run are destroyed. The body
// must not destroy region-owned elements itself.
class Region {
public:
    using Body = std::function<void(RegionScope&)>;

    Region() = default;
    [[nodiscard]] static base::Result<Region> create(Context& context, ElementId parent,
                                                     Body body, RegionOptions options = {});
    [[nodiscard]] ElementId element() const noexcept { return element_; }
    [[nodiscard]] base::Status invalidate();
    [[nodiscard]] base::Status set_live(bool live);

private:
    Region(Context& context, ElementId element) : context_(&context), element_(element) {}
    Context* context_ = nullptr;
    ElementId element_;
};

// Describes one run of a region. Results report user actions since the
// previous run. After a failure the remaining calls do nothing and the run
// reports the first error from `Context::prepare_frame`.
class RegionScope {
public:
    struct State;
    explicit RegionScope(State& state) noexcept : state_(state) {}

    void label(std::string_view id, std::string_view text);
    // True when the button was activated since the previous run.
    [[nodiscard]] bool button(std::string_view id, std::string_view text);
    // Two-way: user toggles update `checked` and return true; otherwise the
    // widget follows `checked`.
    bool checkbox(std::string_view id, std::string_view text, bool& checked);
    bool slider(std::string_view id, float& value, float min, float max, float step = 0.01F);
    bool text_edit(std::string_view id, std::string& value, bool multiline = false);
    void row(std::string_view id, float spacing, const std::function<void()>& body);
    void column(std::string_view id, float spacing, const std::function<void()>& body);

    // Element of the latest item, for layout parameters or further styling.
    [[nodiscard]] ElementId last() const noexcept;
    [[nodiscard]] Context& context() const noexcept;

private:
    State& state_;
};

} // namespace truffle::gui

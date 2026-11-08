#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/painter.hpp"
#include "truffle/gui/text_edit.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

// Fluent, container-first API over the retained element tree.
//
//   gui::Ui ui{context, {.engine = &text, .font = font, .atlas = atlas}};
//   auto tools = ui.root().column();
//   tools.button("Extrude").on_click([&] { doc.extrude(); });
//
// - `container.xxx(...)` creates a child once and returns its typed handle.
//   Nothing is rebuilt per frame; the Context owns the elements.
// - Handles are cheap, copyable and non-owning. Dropping one keeps the
//   element; `remove()` destroys it and its subtree.
// - Setters chain. Programmatic setters never invoke user callbacks.
// - Every size and style is Truffle's default unless set on the handle, the
//   component style or the theme.
// - The fluent layer does not throw. A failed call reports its Status once to
//   the Ui's error handler and returns an invalid handle; calls on an invalid
//   or removed handle do nothing. The tree is never left half-built.
namespace truffle::gui {

namespace detail {
struct UiState;
struct UiNode;
} // namespace detail

class Container;
class Canvas;

using ErrorHandler = std::function<void(const base::Status&)>;

// Shared by every handle type.
class Element {
public:
    Element() = default;

    // False for a failed creation and once the element was removed.
    [[nodiscard]] bool valid() const noexcept;
    // Escape hatch to the core Context API; empty when invalid.
    [[nodiscard]] ElementId element() const noexcept;
    // Destroys the element and its subtree; the handle becomes invalid.
    void remove();

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool visible() const;
    // Resolved style property, including overrides; empty when unavailable.
    [[nodiscard]] std::optional<ThemeValue> property(Property property) const;

protected:
    Element(std::shared_ptr<detail::UiState> ui, std::shared_ptr<detail::UiNode> node)
        : ui_(std::move(ui)), node_(std::move(node)) {}

    // Returns the live node, reporting a stale handle once.
    [[nodiscard]] detail::UiNode* live() const;
    void set_width(float value);
    void set_height(float value);
    void set_min_size(Size value);
    void set_max_size(Size value);
    void set_weight(float value);
    void set_padding(Edges value);
    void set_cell(GridCell value);
    void set_position(Point value);
    void set_enabled(bool value);
    void set_visible(bool value);
    void set_style(const ComponentStyle& style);
    void set_property(Property property, ThemeValue value);
    void reset_property(Property property);
    void set_hover(std::function<void(bool)> callback);
    void set_press(std::function<void()> callback);
    void set_release(std::function<void()> callback);
    void set_paint(PaintFunction paint, PaintLayer layer);
    void invalidate_paint();

    std::shared_ptr<detail::UiState> ui_;
    std::shared_ptr<detail::UiNode> node_;

    friend class Ui;
    friend class Container;
    friend class Menu;
    friend class MenuBar;
};

// Chainable setters common to every handle. `Self` is the handle type.
template <typename Self>
class ElementBase : public Element {
public:
    ElementBase() = default;

    // Fixed size along an axis; content-sized when never set.
    Self& width(float value) { set_width(value); return self(); }
    Self& height(float value) { set_height(value); return self(); }
    Self& size(Size value) { set_width(value.width); set_height(value.height); return self(); }
    Self& min_size(Size value) { set_min_size(value); return self(); }
    Self& max_size(Size value) { set_max_size(value); return self(); }
    // Share of leftover space inside a row or column.
    Self& weight(float value) { set_weight(value); return self(); }
    // Overrides the component's styled padding for this element.
    Self& padding(Edges value) { set_padding(value); return self(); }
    Self& padding(float all) { set_padding({all, all, all, all}); return self(); }
    // Cell inside a grid container.
    Self& cell(GridCell value) { set_cell(value); return self(); }
    // Position inside a panel or canvas (free layout).
    Self& position(Point value) { set_position(value); return self(); }

    using Element::enabled;
    using Element::visible;
    Self& enabled(bool value) { set_enabled(value); return self(); }
    Self& visible(bool value) { set_visible(value); return self(); }

    // Per-element style layer over the component style and the theme.
    Self& style(const ComponentStyle& value) { set_style(value); return self(); }
    using Element::property;
    // Direct per-property override; `reset` resumes the styled value.
    Self& property(Property name, ThemeValue value) {
        set_property(name, std::move(value));
        return self();
    }
    Self& reset(Property name) { reset_property(name); return self(); }

    // Pointer enter (true) and leave (false).
    Self& on_hover(std::function<void(bool)> callback) {
        set_hover(std::move(callback));
        return self();
    }
    // Primary pointer button pressed / released over the element.
    Self& on_press(std::function<void()> callback) {
        set_press(std::move(callback));
        return self();
    }
    Self& on_release(std::function<void()> callback) {
        set_release(std::move(callback));
        return self();
    }
    // Retained custom drawing (see Painter); one function per layer, an empty
    // function removes it.
    Self& on_paint(PaintFunction paint, PaintLayer layer = PaintLayer::foreground) {
        set_paint(std::move(paint), layer);
        return self();
    }
    // Re-runs this element's paint functions before the next frame.
    Self& invalidate() { invalidate_paint(); return self(); }

protected:
    using Element::Element;

private:
    Self& self() { return static_cast<Self&>(*this); }
};

class Label : public ElementBase<Label> {
public:
    Label() = default;
    [[nodiscard]] std::string text() const;
    Label& text(std::string value);

private:
    using ElementBase::ElementBase;
    friend class Container;
};

class Button : public ElementBase<Button> {
public:
    Button() = default;
    [[nodiscard]] std::string text() const;
    Button& text(std::string value);
    // Pointer release inside the button, or Enter/Space while focused.
    Button& on_click(std::function<void()> callback);

private:
    using ElementBase::ElementBase;
    friend class Container;
};

class Checkbox : public ElementBase<Checkbox> {
public:
    Checkbox() = default;
    [[nodiscard]] std::string text() const;
    Checkbox& text(std::string value);
    [[nodiscard]] bool checked() const;
    Checkbox& checked(bool value);
    Checkbox& on_change(std::function<void(bool)> callback);

private:
    using ElementBase::ElementBase;
    friend class Container;
};

class Slider : public ElementBase<Slider> {
public:
    Slider() = default;
    [[nodiscard]] float value() const;
    Slider& value(float value);
    // Keyboard step; must be finite and positive.
    Slider& step(float value);
    Slider& on_change(std::function<void(float)> callback);

private:
    using ElementBase::ElementBase;
    friend class Container;
};

class TextField : public ElementBase<TextField> {
public:
    TextField() = default;
    [[nodiscard]] std::string text() const;
    TextField& text(std::string value);
    TextField& read_only(bool value);
    // User edits only.
    TextField& on_change(std::function<void(const std::string&)> callback);
    // Enter in a single-line field.
    TextField& on_submit(std::function<void()> callback);
    // Lower-level editing access (selection, undo, caret).
    [[nodiscard]] TextEdit editor() const;

private:
    using ElementBase::ElementBase;
    friend class Container;
};

class Image : public ElementBase<Image> {
public:
    Image() = default;
    [[nodiscard]] TextureId texture() const;

private:
    using ElementBase::ElementBase;
    friend class Container;
};

// An empty element for custom painting; size it explicitly or with weight.
class Canvas : public ElementBase<Canvas> {
public:
    Canvas() = default;

private:
    using ElementBase::ElementBase;
    friend class Container;
    friend class Ui;
};

class MenuItem : public ElementBase<MenuItem> {
public:
    MenuItem() = default;
    [[nodiscard]] std::string text() const;
    MenuItem& text(std::string value);
    // Display only; the application owns shortcut bindings.
    MenuItem& shortcut(std::string value);
    // Shows a checkmark that toggles on activation and reports `on_change`.
    MenuItem& checkable(bool checked = false);
    [[nodiscard]] bool checked() const;
    MenuItem& checked(bool value);
    MenuItem& on_click(std::function<void()> callback);
    MenuItem& on_change(std::function<void(bool)> callback);

private:
    using ElementBase::ElementBase;
    friend class Menu;
};

class Menu : public ElementBase<Menu> {
public:
    Menu() = default;
    MenuItem item(std::string text);
    // A nested menu behind an arrow-bearing item.
    Menu menu(std::string text);
    void separator();
    [[nodiscard]] bool is_open() const;
    // The button or item that opens this menu.
    [[nodiscard]] ElementId trigger() const;
    Menu& open();
    Menu& close();

private:
    using ElementBase::ElementBase;
    friend class MenuBar;
    friend class Container;
};

class MenuBar : public ElementBase<MenuBar> {
public:
    MenuBar() = default;
    Menu menu(std::string label);

private:
    using ElementBase::ElementBase;
    friend class Container;
};

// Holds children. Rows, columns and grids space them with the theme's `gui.spacing`
// gap unless `gap` overrides it; panels are styled column surfaces.
class Container : public ElementBase<Container> {
public:
    Container() = default;

    Container panel();
    Container row();
    Container column();
    // Cell gaps follow `gui.spacing` unless `options` sets a spacing.
    Container grid(GridOptions options);
    // A column that scrolls when its content is larger than the container.
    Container scroll();

    Label label(std::string text);
    Button button(std::string text);
    Checkbox checkbox(std::string text, bool checked = false);
    Slider slider(float min, float max, float value);
    TextField text_field(std::string text = {});
    TextField text_area(std::string text = {});
    Image image(TextureId texture, Size size);
    Canvas canvas();
    MenuBar menu_bar();
    // A dropdown button with its menu.
    Menu menu_button(std::string label);

    // Gap between stacked or grid children; overrides `gui.spacing`.
    Container& gap(float value);
    // Scroll offset (scroll containers only).
    [[nodiscard]] Point scroll_offset() const;
    Container& scroll_offset(Point value);

protected:
    using ElementBase::ElementBase;
    friend class Ui;
};

// Entry point: wraps a Context with text defaults and an error handler. Copies
// share state. The Context and text engine must outlive every handle.
class Ui {
public:
    // Gives the root a vertical stack whose gap follows `gui.spacing`.
    explicit Ui(Context& context, TextStyle text = {});

    [[nodiscard]] Context& context() const noexcept;
    [[nodiscard]] const TextStyle& text() const noexcept;

    // The root element as a column container.
    [[nodiscard]] Container root() const;
    // Wraps an existing element without changing its layout, so children can be
    // added to it (for example under a custom-drawn element).
    [[nodiscard]] Container adopt(ElementId element) const;
    // A viewport-sized canvas drawn above every element; takes no input.
    [[nodiscard]] Canvas overlay() const;

    // Default: write to stderr and assert in debug builds.
    void set_error_handler(ErrorHandler handler);
    [[nodiscard]] const base::Status& last_error() const noexcept;
    // Reports through the error handler, as fluent calls do.
    void report(const base::Status& status) const;

private:
    std::shared_ptr<detail::UiState> state_;
};

} // namespace truffle::gui

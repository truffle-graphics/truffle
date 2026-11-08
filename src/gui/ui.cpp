#include "truffle/gui/ui.hpp"

#include "menu_object.hpp"
#include "widget_object.hpp"

#include "truffle/gui/themes.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>

namespace truffle::gui {
namespace detail {

enum class NodeKind : std::uint8_t {
    container, label, button, checkbox, slider, text_field, image, canvas,
    menu_bar, menu, menu_item
};

// User callbacks. Detail objects and event handlers hold these strongly so
// callbacks survive dropped handles; they never refer back to their node.
struct Slots {
    std::function<void()> click;
    std::function<void()> submit;
    std::function<void()> press;
    std::function<void()> release;
    std::function<void(bool)> hover;
    std::function<void(bool)> toggle;
    std::function<void(float)> value;
    std::function<void(const std::string&)> text;
};

struct UiNode {
    NodeKind kind = NodeKind::container;
    ElementId element;
    ElementId content; // where children go (differs for scroll containers)
    Widget widget;
    MenuBar bar;
    Menu menu;
    MenuItem item;
    TextureId texture;
    std::shared_ptr<Slots> slots = std::make_shared<Slots>();
    bool hooks = false;
    bool stale_reported = false;
};

struct UiState {
    Context* context = nullptr;
    TextStyle text;
    ErrorHandler handler;
    base::Status last_error = base::Status::success();
    std::shared_ptr<UiNode> overlay;

    void report(const base::Status& status) {
        last_error = status;
        if (handler) {
            handler(status);
            return;
        }
        std::fprintf(stderr, "truffle gui: %s\n", status.message.c_str());
        assert(false && "Truffle GUI call failed; see stderr or set Ui::set_error_handler");
    }

    // Reports a failure and yields false; success yields true.
    bool check(const base::Status& status) {
        if (!status.ok()) { report(status); }
        return status.ok();
    }

    [[nodiscard]] MenuTextStyle menu_text() const {
        return {text.engine, text.font, text.atlas};
    }
};

} // namespace detail

namespace {

using detail::NodeKind;
using detail::UiNode;
using detail::UiState;

base::Status failure(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

std::shared_ptr<UiNode> make_node(NodeKind kind, ElementId element, ElementId content = {}) {
    auto node = std::make_shared<UiNode>();
    node->kind = kind;
    node->element = element;
    node->content = content.valid() ? content : element;
    return node;
}

// A stack whose gap follows `gui.spacing` until overridden.
base::Status make_stack(Context& context, ElementId element, Axis axis) {
    auto status = install_default_theme(context);
    if (!status.ok()) { return status; }
    status = context.set_layout(element, std::make_shared<StackLayout>(
        StackOptions{.axis = axis, .cross = Align::stretch}));
    if (!status.ok()) { return status; }
    return context.bind_property(element, Property::spacing, "gui.spacing");
}

// Destroys a partially built element after a failed setup step.
base::Status undo(Context& context, ElementId element, base::Status status) {
    if (context.alive(element)) { static_cast<void>(context.destroy(element)); }
    return status;
}

detail::WidgetDesc widget_desc(const UiState& ui, detail::WidgetKind kind, std::string text) {
    detail::WidgetDesc desc;
    desc.kind = kind;
    desc.text = std::move(text);
    desc.text_engine = ui.text.engine;
    desc.font = ui.text.font;
    desc.atlas = ui.text.atlas;
    return desc;
}

void install_hooks(Context& context, UiNode& node) {
    if (node.hooks) { return; }
    auto original = context.event_handler(node.element);
    if (!original.ok()) { return; }
    auto interaction = context.interaction(node.element);
    if (interaction.ok() && !interaction.value().hit_test) {
        interaction.value().hit_test = true;
        static_cast<void>(context.set_interaction(node.element, interaction.value()));
    }
    const auto element = node.element;
    static_cast<void>(context.set_event_handler(node.element,
        [slots = node.slots, handler = std::move(original).value(), element](
            Context& c, RoutedEvent& event) {
            if (const auto* hover = std::get_if<HoverEvent>(&event.payload)) {
                if (slots->hover) { slots->hover(hover->entered); }
            } else if (const auto* pointer = event.input<PointerEvent>();
                       pointer != nullptr && pointer->button == PointerButton::primary &&
                       event.target == element) {
                if (pointer->action == PointerAction::down && slots->press) {
                    slots->press();
                } else if (pointer->action == PointerAction::up && slots->release) {
                    slots->release();
                }
            }
            if (handler) { handler(c, event); }
        }));
    node.hooks = true;
}

} // namespace

// ---- Element ---------------------------------------------------------------

bool Element::valid() const noexcept {
    return node_ && ui_ && ui_->context->alive(node_->element);
}

ElementId Element::element() const noexcept {
    return valid() ? node_->element : ElementId{};
}

detail::UiNode* Element::live() const {
    if (!node_ || !ui_) { return nullptr; }
    if (!ui_->context->alive(node_->element)) {
        if (!node_->stale_reported) {
            node_->stale_reported = true;
            ui_->report(failure("GUI handle refers to a removed element"));
        }
        return nullptr;
    }
    return node_.get();
}

void Element::remove() {
    if (auto* node = live()) {
        ui_->check(ui_->context->destroy(node->element));
    }
    node_.reset();
}

bool Element::enabled() const {
    const auto* node = live();
    if (node == nullptr) { return false; }
    const auto interaction = ui_->context->interaction(node->element);
    return interaction.ok() && interaction.value().enabled;
}

bool Element::visible() const {
    const auto* node = live();
    return node != nullptr && ui_->context->visible(node->element);
}

std::optional<ThemeValue> Element::property(Property property) const {
    const auto* node = live();
    if (node == nullptr) { return std::nullopt; }
    auto value = ui_->context->property(node->element, property);
    if (!value.ok()) { return std::nullopt; }
    return std::move(value).value();
}

namespace {

template <typename Edit>
void edit_params(UiState& ui, const UiNode& node, Edit edit) {
    auto params = ui.context->layout_params(node.element);
    if (!ui.check(params.status())) { return; }
    edit(params.value());
    ui.check(ui.context->set_layout_params(node.element, params.value()));
}

bool finite_size(float value) { return std::isfinite(value) && value >= 0.0F; }

} // namespace

void Element::set_width(float value) {
    auto* node = live();
    if (node == nullptr) { return; }
    if (!finite_size(value)) { ui_->report(failure("GUI width must be finite and >= 0")); return; }
    edit_params(*ui_, *node, [value](LayoutParams& p) { p.width = value; });
}

void Element::set_height(float value) {
    auto* node = live();
    if (node == nullptr) { return; }
    if (!finite_size(value)) { ui_->report(failure("GUI height must be finite and >= 0")); return; }
    edit_params(*ui_, *node, [value](LayoutParams& p) { p.height = value; });
}

void Element::set_min_size(Size value) {
    if (auto* node = live()) {
        edit_params(*ui_, *node, [value](LayoutParams& p) { p.min = value; });
    }
}

void Element::set_max_size(Size value) {
    if (auto* node = live()) {
        edit_params(*ui_, *node, [value](LayoutParams& p) { p.max = value; });
    }
}

void Element::set_weight(float value) {
    if (auto* node = live()) {
        edit_params(*ui_, *node, [value](LayoutParams& p) { p.weight = value; });
    }
}

void Element::set_cell(GridCell value) {
    if (auto* node = live()) {
        edit_params(*ui_, *node, [value](LayoutParams& p) { p.cell = value; });
    }
}

void Element::set_position(Point value) {
    if (auto* node = live()) {
        edit_params(*ui_, *node, [value](LayoutParams& p) { p.position = value; });
    }
}

void Element::set_padding(Edges value) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_property_override(node->element, Property::padding, value));
    }
}

void Element::set_enabled(bool value) {
    auto* node = live();
    if (node == nullptr) { return; }
    switch (node->kind) {
    case NodeKind::label: case NodeKind::button: case NodeKind::checkbox:
    case NodeKind::slider: case NodeKind::text_field: case NodeKind::image:
        ui_->check(node->widget.set_enabled(value));
        return;
    case NodeKind::menu_item:
        ui_->check(node->item.set_enabled(value));
        return;
    default: break;
    }
    if (node->widget.alive()) {
        ui_->check(node->widget.set_enabled(value));
        return;
    }
    auto interaction = ui_->context->interaction(node->element);
    if (!ui_->check(interaction.status())) { return; }
    interaction.value().enabled = value;
    ui_->check(ui_->context->set_interaction(node->element, interaction.value()));
}

void Element::set_visible(bool value) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_visible(node->element, value));
    }
}

void Element::set_style(const ComponentStyle& style) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_component_style(node->element, style));
    }
}

void Element::set_property(Property property, ThemeValue value) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_property_override(node->element, property,
                                                       std::move(value)));
    }
}

void Element::reset_property(Property property) {
    if (auto* node = live()) {
        ui_->check(ui_->context->clear_property_override(node->element, property));
    }
}

void Element::set_hover(std::function<void(bool)> callback) {
    if (auto* node = live()) {
        node->slots->hover = std::move(callback);
        install_hooks(*ui_->context, *node);
    }
}

void Element::set_press(std::function<void()> callback) {
    if (auto* node = live()) {
        node->slots->press = std::move(callback);
        install_hooks(*ui_->context, *node);
    }
}

void Element::set_release(std::function<void()> callback) {
    if (auto* node = live()) {
        node->slots->release = std::move(callback);
        install_hooks(*ui_->context, *node);
    }
}

void Element::set_paint(PaintFunction paint, PaintLayer layer) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_paint(node->element, layer, std::move(paint)));
    }
}

void Element::invalidate_paint() {
    if (auto* node = live()) {
        ui_->check(ui_->context->invalidate_shapes(node->element));
    }
}

// ---- Widgets -----------------------------------------------------------------

namespace {

std::string widget_text(UiState& ui, UiNode* node) {
    if (node == nullptr) { return {}; }
    auto text = node->widget.text();
    return ui.check(text.status()) ? std::move(text).value() : std::string{};
}

} // namespace

std::string Label::text() const { return widget_text(*ui_, live()); }
Label& Label::text(std::string value) {
    if (auto* node = live()) { ui_->check(node->widget.set_text(std::move(value))); }
    return *this;
}

std::string Button::text() const { return widget_text(*ui_, live()); }
Button& Button::text(std::string value) {
    if (auto* node = live()) { ui_->check(node->widget.set_text(std::move(value))); }
    return *this;
}
Button& Button::on_click(std::function<void()> callback) {
    if (auto* node = live()) { node->slots->click = std::move(callback); }
    return *this;
}

std::string Checkbox::text() const { return widget_text(*ui_, live()); }
Checkbox& Checkbox::text(std::string value) {
    if (auto* node = live()) { ui_->check(node->widget.set_text(std::move(value))); }
    return *this;
}
bool Checkbox::checked() const {
    auto* node = live();
    if (node == nullptr) { return false; }
    const auto checked = node->widget.checked();
    return ui_->check(checked.status()) && checked.value();
}
Checkbox& Checkbox::checked(bool value) {
    if (auto* node = live()) { ui_->check(node->widget.set_checked(value)); }
    return *this;
}
Checkbox& Checkbox::on_change(std::function<void(bool)> callback) {
    if (auto* node = live()) { node->slots->toggle = std::move(callback); }
    return *this;
}

float Slider::value() const {
    auto* node = live();
    if (node == nullptr) { return 0.0F; }
    const auto value = node->widget.value();
    return ui_->check(value.status()) ? value.value() : 0.0F;
}
Slider& Slider::value(float value) {
    if (auto* node = live()) { ui_->check(node->widget.set_value(value)); }
    return *this;
}
Slider& Slider::step(float value) {
    if (auto* node = live()) { ui_->check(node->widget.set_step(value)); }
    return *this;
}
Slider& Slider::on_change(std::function<void(float)> callback) {
    if (auto* node = live()) { node->slots->value = std::move(callback); }
    return *this;
}

std::string TextField::text() const { return widget_text(*ui_, live()); }
TextField& TextField::text(std::string value) {
    if (auto* node = live()) { ui_->check(node->widget.set_text(std::move(value))); }
    return *this;
}
TextField& TextField::read_only(bool value) {
    if (auto* node = live()) {
        auto editor = node->widget.editor();
        if (ui_->check(editor.status())) { ui_->check(editor.value().set_read_only(value)); }
    }
    return *this;
}
TextField& TextField::on_change(std::function<void(const std::string&)> callback) {
    if (auto* node = live()) { node->slots->text = std::move(callback); }
    return *this;
}
TextField& TextField::on_submit(std::function<void()> callback) {
    if (auto* node = live()) { node->slots->submit = std::move(callback); }
    return *this;
}
TextEdit TextField::editor() const {
    auto* node = live();
    if (node == nullptr) { return {}; }
    auto editor = node->widget.editor();
    return ui_->check(editor.status()) ? std::move(editor).value() : TextEdit{};
}

TextureId Image::texture() const {
    const auto* node = live();
    return node == nullptr ? TextureId{} : node->texture;
}

// ---- Menus -------------------------------------------------------------------

std::string MenuItem::text() const {
    auto* node = live();
    if (node == nullptr) { return {}; }
    auto text = node->item.text();
    return ui_->check(text.status()) ? std::move(text).value() : std::string{};
}
MenuItem& MenuItem::text(std::string value) {
    if (auto* node = live()) { ui_->check(node->item.set_text(std::move(value))); }
    return *this;
}
MenuItem& MenuItem::shortcut(std::string value) {
    if (auto* node = live()) { ui_->check(node->item.set_shortcut(std::move(value))); }
    return *this;
}
MenuItem& MenuItem::checkable(bool checked) {
    if (auto* node = live()) {
        if (!ui_->check(node->item.set_checked(checked))) { return *this; }
        ui_->check(node->item.set_toggles(true, [slots = node->slots](bool value) {
            if (slots->toggle) { slots->toggle(value); }
        }));
    }
    return *this;
}
bool MenuItem::checked() const {
    auto* node = live();
    if (node == nullptr) { return false; }
    const auto checked = node->item.checked();
    return ui_->check(checked.status()) && checked.value().value_or(false);
}
MenuItem& MenuItem::checked(bool value) {
    if (auto* node = live()) { ui_->check(node->item.set_checked(value)); }
    return *this;
}
MenuItem& MenuItem::on_click(std::function<void()> callback) {
    if (auto* node = live()) { node->slots->click = std::move(callback); }
    return *this;
}
MenuItem& MenuItem::on_change(std::function<void(bool)> callback) {
    if (auto* node = live()) { node->slots->toggle = std::move(callback); }
    return *this;
}

namespace {

detail::MenuItemDesc item_desc(const std::shared_ptr<UiNode>& node, std::string text) {
    detail::MenuItemDesc desc;
    desc.text = std::move(text);
    desc.activated = [slots = node->slots] {
        if (slots->click) { slots->click(); }
    };
    return desc;
}

} // namespace

MenuItem Menu::item(std::string text) {
    auto* node = live();
    if (node == nullptr) { return {}; }
    auto made = make_node(NodeKind::menu_item, {});
    auto item = node->menu.add_item(item_desc(made, std::move(text)));
    if (!ui_->check(item.status())) { return {}; }
    made->item = std::move(item).value();
    made->element = made->content = made->item.element();
    return MenuItem{ui_, std::move(made)};
}

Menu Menu::menu(std::string text) {
    auto* node = live();
    if (node == nullptr) { return {}; }
    auto submenu = node->menu.add_submenu(std::move(text));
    if (!ui_->check(submenu.status())) { return {}; }
    auto made = make_node(NodeKind::menu, submenu.value().element());
    made->menu = std::move(submenu).value();
    return Menu{ui_, std::move(made)};
}

void Menu::separator() {
    if (auto* node = live()) { ui_->check(node->menu.add_separator().status()); }
}

bool Menu::is_open() const {
    const auto* node = live();
    return node != nullptr && node->menu.is_open();
}

ElementId Menu::trigger() const {
    const auto* node = live();
    return node == nullptr ? ElementId{} : node->menu.trigger();
}

Menu& Menu::open() {
    if (auto* node = live()) { ui_->check(node->menu.open()); }
    return *this;
}

Menu& Menu::close() {
    if (auto* node = live()) { ui_->check(node->menu.close()); }
    return *this;
}

Menu MenuBar::menu(std::string label) {
    auto* node = live();
    if (node == nullptr) { return {}; }
    auto menu = node->bar.add_menu(std::move(label));
    if (!ui_->check(menu.status())) { return {}; }
    auto made = make_node(NodeKind::menu, menu.value().element());
    made->menu = std::move(menu).value();
    return Menu{ui_, std::move(made)};
}

// ---- Container ---------------------------------------------------------------

namespace {

// Creates a detail widget under `parent` with callbacks forwarding to `node`.
base::Result<std::shared_ptr<UiNode>> make_widget(UiState& ui, ElementId parent,
                                                  NodeKind kind, detail::WidgetDesc desc) {
    auto node = make_node(kind, {});
    const auto slots = node->slots;
    desc.activated = [slots] {
        if (slots->click) { slots->click(); }
        if (slots->submit) { slots->submit(); }
    };
    desc.toggled = [slots](bool value) { if (slots->toggle) { slots->toggle(value); } };
    desc.value_changed = [slots](float value) { if (slots->value) { slots->value(value); } };
    desc.text_changed = [slots](const std::string& value) {
        if (slots->text) { slots->text(value); }
    };
    auto widget = detail::Widget::create(*ui.context, parent, std::move(desc));
    if (!widget.ok()) { return widget.status(); }
    node->widget = std::move(widget).value();
    node->element = node->widget.element();
    node->content = node->widget.content();
    return node;
}

} // namespace

#define TFL_UI_PARENT()                                                                    \
    auto* parent = live();                                                                 \
    if (parent == nullptr) { return {}; }                                                  \
    [[maybe_unused]] Context& context = *ui_->context

Container Container::panel() {
    TFL_UI_PARENT();
    auto made = make_widget(*ui_, parent->content, NodeKind::container,
                            widget_desc(*ui_, detail::WidgetKind::panel, {}));
    if (!ui_->check(made.status())) { return {}; }
    auto node = std::move(made).value();
    if (auto status = make_stack(context, node->element, Axis::vertical); !status.ok()) {
        ui_->report(undo(context, node->element, status));
        return {};
    }
    return Container{ui_, std::move(node)};
}

Container Container::row() {
    TFL_UI_PARENT();
    auto created = context.create(parent->content);
    if (!ui_->check(created.status())) { return {}; }
    if (auto status = make_stack(context, created.value(), Axis::horizontal); !status.ok()) {
        ui_->report(undo(context, created.value(), status));
        return {};
    }
    return Container{ui_, make_node(NodeKind::container, created.value())};
}

Container Container::column() {
    TFL_UI_PARENT();
    auto created = context.create(parent->content);
    if (!ui_->check(created.status())) { return {}; }
    if (auto status = make_stack(context, created.value(), Axis::vertical); !status.ok()) {
        ui_->report(undo(context, created.value(), status));
        return {};
    }
    return Container{ui_, make_node(NodeKind::container, created.value())};
}

Container Container::grid(GridOptions options) {
    TFL_UI_PARENT();
    auto created = context.create(parent->content);
    if (!ui_->check(created.status())) { return {}; }
    const bool themed_gap = options.column_spacing == 0.0F && options.row_spacing == 0.0F;
    auto status = install_default_theme(context);
    if (status.ok()) {
        status = context.set_layout(created.value(),
                                    std::make_shared<GridLayout>(std::move(options)));
    }
    if (status.ok() && themed_gap) {
        status = context.bind_property(created.value(), Property::spacing, "gui.spacing");
    }
    if (!status.ok()) {
        ui_->report(undo(context, created.value(), status));
        return {};
    }
    return Container{ui_, make_node(NodeKind::container, created.value())};
}

Container Container::scroll() {
    TFL_UI_PARENT();
    auto made = make_widget(*ui_, parent->content, NodeKind::container,
                            widget_desc(*ui_, detail::WidgetKind::scroll_area, {}));
    if (!ui_->check(made.status())) { return {}; }
    auto node = std::move(made).value();
    if (auto status = make_stack(context, node->content, Axis::vertical); !status.ok()) {
        ui_->report(undo(context, node->element, status));
        return {};
    }
    return Container{ui_, std::move(node)};
}

Label Container::label(std::string text) {
    TFL_UI_PARENT();
    auto made = make_widget(*ui_, parent->content, NodeKind::label,
                            widget_desc(*ui_, detail::WidgetKind::label, std::move(text)));
    return ui_->check(made.status()) ? Label{ui_, std::move(made).value()} : Label{};
}

Button Container::button(std::string text) {
    TFL_UI_PARENT();
    auto made = make_widget(*ui_, parent->content, NodeKind::button,
                            widget_desc(*ui_, detail::WidgetKind::button, std::move(text)));
    return ui_->check(made.status()) ? Button{ui_, std::move(made).value()} : Button{};
}

Checkbox Container::checkbox(std::string text, bool checked) {
    TFL_UI_PARENT();
    auto desc = widget_desc(*ui_, detail::WidgetKind::checkbox, std::move(text));
    desc.checked = checked;
    auto made = make_widget(*ui_, parent->content, NodeKind::checkbox, std::move(desc));
    return ui_->check(made.status()) ? Checkbox{ui_, std::move(made).value()} : Checkbox{};
}

Slider Container::slider(float min, float max, float value) {
    TFL_UI_PARENT();
    auto desc = widget_desc(*ui_, detail::WidgetKind::slider, {});
    desc.min = min;
    desc.max = max;
    desc.value = value;
    auto made = make_widget(*ui_, parent->content, NodeKind::slider, std::move(desc));
    return ui_->check(made.status()) ? Slider{ui_, std::move(made).value()} : Slider{};
}

namespace {

base::Result<std::shared_ptr<UiNode>> make_text(UiState& ui, ElementId parent,
                                                std::string text, bool multiline) {
    auto desc = widget_desc(ui, detail::WidgetKind::text_edit, std::move(text));
    desc.multiline = multiline;
    return make_widget(ui, parent, NodeKind::text_field, std::move(desc));
}

} // namespace

TextField Container::text_field(std::string text) {
    TFL_UI_PARENT();
    auto made = make_text(*ui_, parent->content, std::move(text), false);
    return ui_->check(made.status()) ? TextField{ui_, std::move(made).value()} : TextField{};
}

TextField Container::text_area(std::string text) {
    TFL_UI_PARENT();
    auto made = make_text(*ui_, parent->content, std::move(text), true);
    return ui_->check(made.status()) ? TextField{ui_, std::move(made).value()} : TextField{};
}

Image Container::image(TextureId texture, Size size) {
    TFL_UI_PARENT();
    auto desc = widget_desc(*ui_, detail::WidgetKind::image, {});
    desc.image = texture;
    desc.image_size = size;
    auto made = make_widget(*ui_, parent->content, NodeKind::image, std::move(desc));
    if (!ui_->check(made.status())) { return {}; }
    made.value()->texture = texture;
    return Image{ui_, std::move(made).value()};
}

Canvas Container::canvas() {
    TFL_UI_PARENT();
    auto created = context.create(parent->content);
    if (!ui_->check(created.status())) { return {}; }
    return Canvas{ui_, make_node(NodeKind::canvas, created.value())};
}

MenuBar Container::menu_bar() {
    TFL_UI_PARENT();
    auto bar = detail::MenuBar::create(context, parent->content, ui_->menu_text());
    if (!ui_->check(bar.status())) { return {}; }
    auto node = make_node(NodeKind::menu_bar, bar.value().element());
    node->bar = std::move(bar).value();
    return MenuBar{ui_, std::move(node)};
}

Menu Container::menu_button(std::string label) {
    TFL_UI_PARENT();
    auto button = detail::MenuButton::create(context, parent->content, std::move(label),
                                             ui_->menu_text());
    if (!ui_->check(button.status())) { return {}; }
    auto menu = button.value().menu();
    if (!ui_->check(menu.status())) {
        static_cast<void>(undo(context, button.value().element(), menu.status()));
        return {};
    }
    auto node = make_node(NodeKind::menu, menu.value().element());
    node->menu = std::move(menu).value();
    return Menu{ui_, std::move(node)};
}

#undef TFL_UI_PARENT

Container& Container::gap(float value) {
    if (auto* node = live()) {
        ui_->check(ui_->context->set_property_override(node->content, Property::spacing, value));
    }
    return *this;
}

Point Container::scroll_offset() const {
    auto* node = live();
    if (node == nullptr || !node->widget.alive()) { return {}; }
    const auto offset = node->widget.scroll();
    return offset.ok() ? offset.value() : Point{};
}

Container& Container::scroll_offset(Point value) {
    if (auto* node = live()) {
        if (!node->widget.alive() || node->content == node->element) {
            ui_->report(failure("GUI scroll offset needs a scroll container"));
        } else {
            ui_->check(node->widget.set_scroll(value));
        }
    }
    return *this;
}

// ---- Ui ----------------------------------------------------------------------

Ui::Ui(Context& context, TextStyle text) : state_(std::make_shared<UiState>()) {
    state_->context = &context;
    state_->text = text;
    state_->check(make_stack(context, context.root(), Axis::vertical));
}

Context& Ui::context() const noexcept { return *state_->context; }
const TextStyle& Ui::text() const noexcept { return state_->text; }

Container Ui::root() const {
    return Container{state_, make_node(NodeKind::container, state_->context->root())};
}

Container Ui::adopt(ElementId element) const {
    if (!state_->context->alive(element)) {
        state_->report(failure("GUI adopt needs a live element"));
        return {};
    }
    return Container{state_, make_node(NodeKind::container, element)};
}

Canvas Ui::overlay() const {
    Context& context = *state_->context;
    if (state_->overlay && context.alive(state_->overlay->element)) {
        return Canvas{state_, state_->overlay};
    }
    auto created = context.create(context.root());
    if (!state_->check(created.status())) { return {}; }
    const ElementId element = created.value();
    auto status = context.set_placement(element, Rect{0, 0, unbounded, unbounded});
    if (status.ok()) { status = context.set_overlay(element, true); }
    if (status.ok()) { status = context.set_interaction(element, {.hit_test = false}); }
    if (!status.ok()) {
        state_->report(undo(context, element, status));
        return {};
    }
    state_->overlay = make_node(NodeKind::canvas, element);
    return Canvas{state_, state_->overlay};
}

void Ui::set_error_handler(ErrorHandler handler) { state_->handler = std::move(handler); }
const base::Status& Ui::last_error() const noexcept { return state_->last_error; }
void Ui::report(const base::Status& status) const { state_->report(status); }

} // namespace truffle::gui

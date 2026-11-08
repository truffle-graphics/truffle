#include "truffle/gui/context.hpp"
#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace truffle::gui {
namespace {

base::Status invalid(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

constexpr std::array color_names{
    "gui.surface", "gui.control", "gui.hover", "gui.pressed", "gui.disabled",
    "gui.accent", "gui.border", "gui.text", "gui.text_disabled", "gui.image",
    "gui.selection", "gui.caret", "gui.composition", "dock.background", "dock.area",
    "dock.tab", "dock.tab_active", "dock.divider", "dock.marker", "dock.marker_border"};
constexpr std::array metric_names{
    "gui.radius", "gui.border_width", "dock.marker_border_width", "dock.radius",
    "dock.tab_radius", "gui.spacing"};
constexpr std::array padding_names{"gui.padding"};
constexpr std::array font_size_names{"gui.font_size"};

template <typename Enum, std::size_t N>
std::string_view token_name(Enum token, const std::array<const char*, N>& names) {
    const auto index = static_cast<std::size_t>(token);
    return index < names.size() ? names[index] : std::string_view{};
}

bool valid_color(Color color) {
    const auto valid = [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    return valid(color.r) && valid(color.g) && valid(color.b) && valid(color.a);
}

bool valid_size(Size size) {
    return std::isfinite(size.width) && std::isfinite(size.height) &&
           size.width >= 0 && size.height >= 0;
}

bool valid_edges(Edges edges) {
    const auto valid = [](float v) { return std::isfinite(v) && v >= 0; };
    return valid(edges.left) && valid(edges.top) && valid(edges.right) && valid(edges.bottom);
}

ComponentStyle make_defaults(Component component) {
    ComponentStyle style;
    style.fill.normal = ColorToken::surface;
    style.border.normal = ColorToken::border;
    style.text.normal = ColorToken::text;
    style.indicator.normal = ColorToken::text;
    style.indicator_border.normal = ColorToken::border;
    style.selection.normal = ColorToken::selection;
    style.caret.normal = ColorToken::caret;
    style.composition.normal = ColorToken::composition;
    style.border_width = MetricToken::border_width;
    style.corner_radius = MetricToken::radius;
    style.font_size = FontSizeToken::standard;
    style.padding = Edges{};
    style.min_size = Size{};

    switch (component) {
    case Component::panel:
    case Component::scroll_area:
        style.padding = PaddingToken::standard;
        break;
    case Component::label:
        style.fill.normal = Color{0, 0, 0, 0};
        style.border_width = 0.0F;
        break;
    case Component::image:
        style.fill.normal = ColorToken::image;
        style.border_width = 0.0F;
        break;
    case Component::button:
    case Component::checkbox:
    case Component::slider:
    case Component::menu_button:
        style.fill.normal = ColorToken::control;
        style.fill.hovered = ColorToken::hover;
        style.fill.pressed = ColorToken::pressed;
        style.fill.disabled = ColorToken::disabled;
        style.border.focused = ColorToken::accent;
        style.min_size = component == Component::slider ? Size{120, 24}
                       : component == Component::menu_button ? Size{32, 28} : Size{80, 32};
        if (component == Component::button || component == Component::menu_button) {
            style.padding = PaddingToken::standard;
        } else if (component == Component::checkbox) {
            style.padding = PaddingToken::standard;
            style.checkbox.indicator_size = Size{16, 16};
            style.checkbox.indicator_gap = 8;
            style.indicator.normal = ColorToken::control;
            style.indicator.checked = ColorToken::accent;
        } else {
            style.border.normal = ColorToken::accent;
            style.slider.track_thickness = 4;
            style.slider.thumb_width = 12;
            style.slider.thumb_inset = 2;
        }
        if (component == Component::menu_button) {
            style.text.disabled = ColorToken::text_disabled;
            style.indicator.disabled = ColorToken::text_disabled;
            style.menu.indicator_size = Size{12, 12};
            style.menu.trailing_width = 12;
            style.menu.column_gap = 6;
        }
        break;
    case Component::text_edit:
    case Component::multiline_text_edit:
        style.border.focused = ColorToken::accent;
        style.padding = Edges{8, 6, 8, 6};
        style.min_size = Size{120, component == Component::text_edit ? 30.0F : 80.0F};
        break;
    case Component::menu:
        style.padding = PaddingToken::standard;
        style.min_size = Size{160, 0};
        break;
    case Component::menu_item:
        style.fill.focused = ColorToken::hover;
        style.fill.disabled = ColorToken::disabled;
        style.text.disabled = ColorToken::text_disabled;
        style.indicator.disabled = ColorToken::text_disabled;
        style.padding = Edges{4, 6, 4, 6};
        style.min_size = Size{0, 28};
        style.menu.indicator_size = Size{12, 12};
        style.menu.leading_width = 16;
        style.menu.trailing_width = 12;
        style.menu.column_gap = 6;
        style.border_width = 0.0F;
        style.corner_radius = 0.0F;
        break;
    case Component::menu_separator:
        style.fill.normal = ColorToken::border;
        style.min_size = Size{0, 9};
        style.border_width = 0.0F;
        style.corner_radius = 0.0F;
        break;
    case Component::menu_bar:
        style.border_width = 0.0F;
        style.corner_radius = 0.0F;
        break;
    case Component::dock_background:
        style.fill.normal = ColorToken::dock_background;
        style.border_width = 0.0F;
        style.corner_radius = 0.0F;
        break;
    case Component::dock_area:
        style.fill.normal = ColorToken::dock_area;
        style.corner_radius = MetricToken::dock_radius;
        style.border_width = 0.0F;
        break;
    case Component::dock_tab:
        style.fill.normal = ColorToken::dock_tab;
        style.fill.checked = ColorToken::dock_tab_active;
        style.corner_radius = MetricToken::dock_tab_radius;
        style.padding = Edges{8, 4, 8, 4};
        style.border_width = 0.0F;
        break;
    case Component::dock_divider:
        style.fill.normal = ColorToken::dock_divider;
        style.border_width = 0.0F;
        style.corner_radius = 0.0F;
        break;
    case Component::dock_marker:
        style.fill.normal = ColorToken::dock_marker;
        style.border.normal = ColorToken::dock_marker_border;
        style.border_width = MetricToken::dock_marker_border_width;
        style.corner_radius = 0.0F;
        break;
    case Component::count:
        break;
    }
    return style;
}

const ComponentStyle& defaults(Component component) {
    static const auto values = [] {
        std::array<ComponentStyle, static_cast<std::size_t>(Component::count)> result;
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = make_defaults(static_cast<Component>(i));
        }
        return result;
    }();
    return values[static_cast<std::size_t>(component)];
}

const StateValues<ColorStyle>& colors(const ComponentStyle& style, StylePart part) {
    switch (part) {
    case StylePart::automatic: break;
    case StylePart::fill: return style.fill;
    case StylePart::border: return style.border;
    case StylePart::text: return style.text;
    case StylePart::indicator: return style.indicator;
    case StylePart::indicator_border: return style.indicator_border;
    case StylePart::selection: return style.selection;
    case StylePart::caret: return style.caret;
    case StylePart::composition: return style.composition;
    case StylePart::shortcut_text: break;
    }
    return style.fill;
}

const ColorStyle* select_color(const StateValues<ColorStyle>& base,
                               const StateValues<ColorStyle>& global,
                               const StateValues<ColorStyle>& local, ComponentState state) {
    using Member = std::optional<ColorStyle> StateValues<ColorStyle>::*;
    const auto pick = [&](Member member) -> const ColorStyle* {
        for (const auto* values : {&local, &global, &base}) {
            if (const auto& value = values->*member) { return &*value; }
        }
        return nullptr;
    };
    if (state.disabled) {
        if (const auto* value = pick(&StateValues<ColorStyle>::disabled)) { return value; }
    }
    if (state.pressed) {
        if (const auto* value = pick(&StateValues<ColorStyle>::pressed)) { return value; }
    }
    if (state.hovered) {
        if (const auto* value = pick(&StateValues<ColorStyle>::hovered)) { return value; }
    }
    if (state.checked) {
        if (const auto* value = pick(&StateValues<ColorStyle>::checked)) { return value; }
    }
    if (state.focused) {
        if (const auto* value = pick(&StateValues<ColorStyle>::focused)) { return value; }
    }
    return pick(&StateValues<ColorStyle>::normal);
}

template <typename T>
const T* select(const std::optional<T>& base, const std::optional<T>& global,
                const std::optional<T>& local) {
    if (local) { return &*local; }
    if (global) { return &*global; }
    return base ? &*base : nullptr;
}

base::Status validate(const Context& context, Component component, const ComponentStyle& style) {
    const auto check_color = [&](const std::optional<ColorStyle>& value) {
        if (!value) { return true; }
        if (const auto* literal = std::get_if<Color>(&*value)) { return valid_color(*literal); }
        return context.theme_value(std::get<ColorToken>(*value)).ok();
    };
    for (const auto* group : {&style.fill, &style.border, &style.text, &style.indicator,
                              &style.indicator_border, &style.selection, &style.caret,
                              &style.composition}) {
        for (const auto* value : {&group->normal, &group->hovered, &group->pressed,
                                  &group->focused, &group->disabled, &group->checked}) {
            if (!check_color(*value)) { return invalid("GUI component color is invalid"); }
        }
    }
    const auto check_metric = [&](const std::optional<MetricStyle>& value) {
        if (!value) { return true; }
        float resolved = 0;
        if (const auto* literal = std::get_if<float>(&*value)) {
            resolved = *literal;
        } else {
            const auto token = context.theme_value(std::get<MetricToken>(*value));
            if (!token.ok()) { return false; }
            resolved = token.value();
        }
        return std::isfinite(resolved) && resolved >= 0;
    };
    if (!check_metric(style.border_width) || !check_metric(style.corner_radius) ||
        (style.min_size && !valid_size(*style.min_size))) {
        return invalid("GUI component metric is invalid");
    }
    if (style.font_size) {
        FontSize size;
        if (const auto* literal = std::get_if<FontSize>(&*style.font_size)) {
            size = *literal;
        } else {
            const auto token = context.theme_value(std::get<FontSizeToken>(*style.font_size));
            if (!token.ok()) { return invalid("GUI component font size token is invalid"); }
            size = token.value();
        }
        if (size.units && (!std::isfinite(*size.units) || *size.units <= 0 ||
                           *size.units > 512)) {
            return invalid("GUI component font size is invalid");
        }
    }
    if (style.menu.shortcut_font_size) {
        FontSize size;
        if (const auto* literal = std::get_if<FontSize>(&*style.menu.shortcut_font_size)) {
            size = *literal;
        } else {
            const auto token =
                context.theme_value(std::get<FontSizeToken>(*style.menu.shortcut_font_size));
            if (!token.ok()) { return invalid("GUI menu shortcut font size token is invalid"); }
            size = token.value();
        }
        if (size.units && (!std::isfinite(*size.units) || *size.units <= 0 ||
                           *size.units > 512)) {
            return invalid("GUI menu shortcut font size is invalid");
        }
    }
    if (style.padding) {
        if (const auto* literal = std::get_if<Edges>(&*style.padding)) {
            if (!valid_edges(*literal)) { return invalid("GUI component padding is invalid"); }
        } else if (!context.theme_value(std::get<PaddingToken>(*style.padding)).ok()) {
            return invalid("GUI component padding token is invalid");
        }
    }
    const auto valid_scalar = [](const std::optional<float>& value) {
        return !value || (std::isfinite(*value) && *value >= 0);
    };
    const bool checkbox = style.checkbox.indicator_size || style.checkbox.indicator_gap;
    const bool slider = style.slider.track_thickness || style.slider.thumb_width ||
                        style.slider.thumb_inset;
    const bool menu = style.menu.indicator_size || style.menu.leading_width ||
                      style.menu.trailing_width || style.menu.column_gap ||
                      style.menu.shortcut_font_size;
    if ((checkbox && component != Component::checkbox) ||
        (slider && component != Component::slider) ||
        (menu && component != Component::menu_button && component != Component::menu_item) ||
        (style.menu.leading_width && component != Component::menu_item) ||
        (style.checkbox.indicator_size && !valid_size(*style.checkbox.indicator_size)) ||
        (style.menu.indicator_size && !valid_size(*style.menu.indicator_size)) ||
        !valid_scalar(style.checkbox.indicator_gap) || !valid_scalar(style.slider.track_thickness) ||
        !valid_scalar(style.slider.thumb_width) || !valid_scalar(style.slider.thumb_inset) ||
        !valid_scalar(style.menu.leading_width) || !valid_scalar(style.menu.trailing_width) ||
        !valid_scalar(style.menu.column_gap)) {
        return invalid("GUI component-specific metrics are invalid");
    }
    return base::Status::success();
}

} // namespace

base::Status Context::set_theme_value(ColorToken token, Color value) {
    const auto name = token_name(token, color_names);
    return name.empty() ? invalid("GUI color token is invalid")
                        : set_theme_value(std::string(name), value);
}

base::Status Context::set_theme_value(MetricToken token, float value) {
    const auto name = token_name(token, metric_names);
    return name.empty() ? invalid("GUI metric token is invalid")
                        : set_theme_value(std::string(name), value);
}

base::Status Context::set_theme_value(PaddingToken token, Edges value) {
    const auto name = token_name(token, padding_names);
    return name.empty() ? invalid("GUI padding token is invalid")
                        : set_theme_value(std::string(name), value);
}

base::Status Context::set_theme_value(FontSizeToken token, FontSize value) {
    const auto name = token_name(token, font_size_names);
    return name.empty() ? invalid("GUI font size token is invalid")
                        : set_theme_value(std::string(name), value);
}

base::Result<Color> Context::theme_value(ColorToken token) const {
    const auto name = token_name(token, color_names);
    const auto value = name.empty() ? base::Result<ThemeValue>{invalid("GUI color token is invalid")}
                                    : theme_value(name);
    if (!value.ok()) { return value.status(); }
    const auto* color = std::get_if<Color>(&value.value());
    return color ? base::Result<Color>{*color} : invalid("GUI color token has the wrong type");
}

base::Result<float> Context::theme_value(MetricToken token) const {
    const auto name = token_name(token, metric_names);
    const auto value = name.empty() ? base::Result<ThemeValue>{invalid("GUI metric token is invalid")}
                                    : theme_value(name);
    if (!value.ok()) { return value.status(); }
    const auto* metric = std::get_if<float>(&value.value());
    return metric ? base::Result<float>{*metric} : invalid("GUI metric token has the wrong type");
}

base::Result<Edges> Context::theme_value(PaddingToken token) const {
    const auto name = token_name(token, padding_names);
    const auto value = name.empty() ? base::Result<ThemeValue>{invalid("GUI padding token is invalid")}
                                    : theme_value(name);
    if (!value.ok()) { return value.status(); }
    const auto* padding = std::get_if<Edges>(&value.value());
    return padding ? base::Result<Edges>{*padding} : invalid("GUI padding token has the wrong type");
}

base::Result<FontSize> Context::theme_value(FontSizeToken token) const {
    const auto name = token_name(token, font_size_names);
    const auto value = name.empty()
        ? base::Result<ThemeValue>{invalid("GUI font size token is invalid")} : theme_value(name);
    if (!value.ok()) { return value.status(); }
    const auto* size = std::get_if<FontSize>(&value.value());
    return size ? base::Result<FontSize>{*size} : invalid("GUI font size token has the wrong type");
}

base::Status Context::set_component(ElementId element, Component component) {
    Node* node = find(element);
    if (node == nullptr || component >= Component::count) {
        return invalid("GUI component needs a live element and valid type");
    }
    if (node->component && node->component->kind != component) {
        return invalid("GUI element already has a different component type");
    }
    auto status = install_default_theme(*this);
    if (!status.ok()) { return status; }
    if (!node->component) {
        node->component = std::make_unique<Node::ComponentData>();
        node->component->kind = component;
        component_owners_[static_cast<std::size_t>(component)].insert(element.value());
    }
    return base::Status::success();
}

base::Status Context::set_component_style(Component component, const ComponentStyle& style) {
    if (component >= Component::count) { return invalid("GUI component type is invalid"); }
    auto status = install_default_theme(*this);
    if (!status.ok()) { return status; }
    status = validate(*this, component, style);
    if (!status.ok()) { return status; }
    auto& current = component_styles_[static_cast<std::size_t>(component)];
    if (current == style) { return base::Status::success(); }
    current = style;
    return refresh_component_styles(component);
}

base::Status Context::set_component_style(ElementId element, const ComponentStyle& style) {
    Node* node = find(element);
    if (node == nullptr || !node->component) { return invalid("GUI component is not alive"); }
    auto status = validate(*this, node->component->kind, style);
    if (!status.ok()) { return status; }
    if (node->component->style == style) { return base::Status::success(); }
    node->component->style = style;
    return refresh_component_styles(element);
}

base::Result<ComponentStyle> Context::component_style(Component component) const {
    return component < Component::count
        ? base::Result<ComponentStyle>{component_styles_[static_cast<std::size_t>(component)]}
        : invalid("GUI component type is invalid");
}

base::Result<ComponentStyle> Context::component_style(ElementId element) const {
    const Node* node = find(element);
    return node && node->component ? base::Result<ComponentStyle>{node->component->style}
                                   : invalid("GUI component is not alive");
}

base::Result<ComponentStyle> Context::component_defaults(Component component) const {
    return component < Component::count ? base::Result<ComponentStyle>{defaults(component)}
                                        : invalid("GUI component type is invalid");
}

base::Status Context::set_component_state(ElementId element, ComponentState state) {
    Node* node = find(element);
    if (node == nullptr || !node->component) { return invalid("GUI component is not alive"); }
    if (node->component->state == state) { return base::Status::success(); }
    node->component->state = state;
    return refresh_component_styles(element);
}

base::Result<ComponentState> Context::component_state(ElementId element) const {
    const Node* node = find(element);
    return node && node->component ? base::Result<ComponentState>{node->component->state}
                                   : invalid("GUI component is not alive");
}

base::Status Context::set_component_style_handler(ElementId element,
                                                   std::function<base::Status()> handler) {
    Node* node = find(element);
    if (node == nullptr || !node->component) { return invalid("GUI component is not alive"); }
    node->component->handler = std::move(handler);
    return base::Status::success();
}

base::Result<ThemeValue> Context::component_property(ElementId element, Property property,
                                                      StylePart part) const {
    const Node* node = find(element);
    if (node == nullptr || !node->component || part > StylePart::shortcut_text) {
        return invalid("GUI component is not alive or style part is invalid");
    }
    const auto& data = *node->component;
    if (part == StylePart::automatic) {
        part = property == Property::border_color ? StylePart::border
             : property == Property::text_color ? StylePart::text
             : property == Property::selection_color ? StylePart::selection
             : property == Property::caret_color ? StylePart::caret
             : property == Property::composition_color ? StylePart::composition
             : StylePart::fill;
    }
    const ComponentStyle& base = defaults(data.kind);
    const auto& global = component_styles_[static_cast<std::size_t>(data.kind)];
    const auto& local = data.style;
    if (property == Property::fill || property == Property::border_color ||
        property == Property::text_color || property == Property::selection_color ||
        property == Property::caret_color || property == Property::composition_color) {
        const auto* value = select_color(colors(base, part), colors(global, part),
                                         colors(local, part), data.state);
        if (value == nullptr) { return invalid("GUI component has no color for this state"); }
        if (const auto* literal = std::get_if<Color>(value)) { return ThemeValue{*literal}; }
        const auto resolved = theme_value(std::get<ColorToken>(*value));
        return resolved.ok() ? base::Result<ThemeValue>{ThemeValue{resolved.value()}}
                             : base::Result<ThemeValue>{resolved.status()};
    }
    if (property == Property::border_width || property == Property::corner_radius) {
        const auto member = property == Property::border_width ? &ComponentStyle::border_width
                                                                : &ComponentStyle::corner_radius;
        const auto* value = select(base.*member, global.*member, local.*member);
        if (value == nullptr) { return invalid("GUI component metric is not defined"); }
        if (const auto* literal = std::get_if<float>(value)) { return ThemeValue{*literal}; }
        const auto resolved = theme_value(std::get<MetricToken>(*value));
        return resolved.ok() ? base::Result<ThemeValue>{ThemeValue{resolved.value()}}
                             : base::Result<ThemeValue>{resolved.status()};
    }
    if (property == Property::font_size) {
        const auto* value = select(base.font_size, global.font_size, local.font_size);
        if (part == StylePart::shortcut_text) {
            if (data.kind != Component::menu_item && data.kind != Component::menu_button) {
                return invalid("GUI shortcut text is a menu item or menu button part");
            }
            const auto* shortcut = select(base.menu.shortcut_font_size,
                                          global.menu.shortcut_font_size,
                                          local.menu.shortcut_font_size);
            if (shortcut != nullptr) { value = shortcut; }
        }
        if (value == nullptr) { return invalid("GUI component font size is not defined"); }
        if (const auto* literal = std::get_if<FontSize>(value)) { return ThemeValue{*literal}; }
        const auto resolved = theme_value(std::get<FontSizeToken>(*value));
        return resolved.ok() ? base::Result<ThemeValue>{ThemeValue{resolved.value()}}
                             : base::Result<ThemeValue>{resolved.status()};
    }
    if (property == Property::padding) {
        const auto* value = select(base.padding, global.padding, local.padding);
        if (value == nullptr) { return invalid("GUI component padding is not defined"); }
        Edges padding;
        if (const auto* literal = std::get_if<Edges>(value)) {
            padding = *literal;
        } else {
            const auto resolved = theme_value(std::get<PaddingToken>(*value));
            if (!resolved.ok()) { return resolved.status(); }
            padding = resolved.value();
        }
        if (data.kind == Component::checkbox && !global.padding && !local.padding) {
            const auto indicator = component_property(element, Property::indicator_size);
            const auto gap = component_property(element, Property::indicator_gap);
            if (!indicator.ok()) { return indicator.status(); }
            if (!gap.ok()) { return gap.status(); }
            const auto& indicator_binding =
                node->styles[static_cast<std::size_t>(Property::indicator_size)];
            const auto& gap_binding = node->styles[static_cast<std::size_t>(Property::indicator_gap)];
            const auto& size_value = indicator_binding.override_value
                ? *indicator_binding.override_value : indicator.value();
            const auto& gap_value = gap_binding.override_value
                ? *gap_binding.override_value : gap.value();
            padding.left += std::get<Size>(size_value).width + std::get<float>(gap_value);
        }
        return ThemeValue{padding};
    }
    if (property == Property::min_size) {
        const auto* value = select(base.min_size, global.min_size, local.min_size);
        return value ? base::Result<ThemeValue>{ThemeValue{*value}}
                     : base::Result<ThemeValue>{invalid("GUI component minimum size is not defined")};
    }
    if (property == Property::indicator_size) {
        const auto* value = data.kind == Component::checkbox
            ? select(base.checkbox.indicator_size, global.checkbox.indicator_size,
                     local.checkbox.indicator_size)
            : select(base.menu.indicator_size, global.menu.indicator_size,
                     local.menu.indicator_size);
        return value ? base::Result<ThemeValue>{ThemeValue{*value}}
                     : base::Result<ThemeValue>{invalid("GUI indicator size is not defined")};
    }
    const float* value = nullptr;
    switch (property) {
    case Property::indicator_gap:
        value = select(base.checkbox.indicator_gap, global.checkbox.indicator_gap,
                       local.checkbox.indicator_gap);
        break;
    case Property::slider_track_thickness:
        value = select(base.slider.track_thickness, global.slider.track_thickness,
                       local.slider.track_thickness);
        break;
    case Property::slider_thumb_width:
        value = select(base.slider.thumb_width, global.slider.thumb_width, local.slider.thumb_width);
        break;
    case Property::slider_thumb_inset:
        value = select(base.slider.thumb_inset, global.slider.thumb_inset, local.slider.thumb_inset);
        break;
    case Property::menu_leading_width:
        value = select(base.menu.leading_width, global.menu.leading_width, local.menu.leading_width);
        break;
    case Property::menu_trailing_width:
        value = select(base.menu.trailing_width, global.menu.trailing_width,
                       local.menu.trailing_width);
        break;
    case Property::menu_column_gap:
        value = select(base.menu.column_gap, global.menu.column_gap, local.menu.column_gap);
        break;
    default: break;
    }
    if (value != nullptr) { return ThemeValue{*value}; }
    return invalid("GUI component property is not supported");
}

base::Status Context::bind_component_property(ElementId element, Property property,
                                               ElementId owner, StylePart part) {
    Node* node = find(element);
    if (node == nullptr || property >= Property::count) {
        return invalid("GUI component binding needs a live element and property");
    }
    const auto value = component_property(owner, property, part);
    if (!value.ok()) { return value.status(); }
    auto& binding = node->styles[static_cast<std::size_t>(property)];
    if (binding.component && binding.component->owner == owner &&
        binding.component->part == part) {
        return base::Status::success();
    }
    if (!binding.name.empty()) {
        auto& old = theme_dependents_[binding.name];
        std::erase_if(old, [element, property](const ThemeDependent& entry) {
            return entry.element == element && entry.property == property;
        });
        binding.name.clear();
    }
    if (binding.component) {
        forget_component_binding(element, property, binding.component->owner);
    }
    binding.component = Node::StyleBinding::ComponentBinding{owner, part};
    component_dependents_[owner.value()].push_back({element, property, owner});
    if (!binding.override_value && binding.resolved != value.value()) {
        mark_style(element, property);
    }
    return base::Status::success();
}

void Context::forget_component_binding(ElementId element, Property property, ElementId owner) {
    const auto found = component_dependents_.find(owner.value());
    if (found == component_dependents_.end()) { return; }
    std::erase_if(found->second, [element, property](const ComponentDependent& entry) {
        return entry.element == element && entry.property == property;
    });
    if (found->second.empty()) { component_dependents_.erase(found); }
}

base::Status Context::refresh_component_styles(ElementId owner) {
    const auto found = component_dependents_.find(owner.value());
    if (found == component_dependents_.end()) { return base::Status::success(); }
    for (const auto& entry : found->second) {
        Node* node = find(entry.element);
        if (node == nullptr) { continue; }
        const auto& binding = node->styles[static_cast<std::size_t>(entry.property)];
        if (!binding.component || binding.override_value) { continue; }
        const auto value = component_property(entry.owner, entry.property, binding.component->part);
        if (!value.ok()) { return value.status(); }
        if (binding.resolved != value.value()) { mark_style(entry.element, entry.property); }
    }
    return base::Status::success();
}

base::Status Context::refresh_component_styles(Component component) {
    for (const auto owner : component_owners_[static_cast<std::size_t>(component)]) {
        const auto status = refresh_component_styles(ElementId{owner});
        if (!status.ok()) { return status; }
    }
    return base::Status::success();
}

base::Status Context::refresh_component_styles() {
    for (const auto& [owner, entries] : component_dependents_) {
        (void)entries;
        const auto status = refresh_component_styles(ElementId{owner});
        if (!status.ok()) { return status; }
    }
    return base::Status::success();
}

} // namespace truffle::gui

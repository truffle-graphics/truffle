#include "truffle/gui/context.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {

base::Status invalid_style(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

bool valid_value(const ThemeValue& value) {
    const auto positive = [](float v) { return std::isfinite(v) && v >= 0; };
    if (const auto* scalar = std::get_if<float>(&value)) { return positive(*scalar); }
    if (const auto* edges = std::get_if<Edges>(&value)) {
        return positive(edges->left) && positive(edges->top) &&
               positive(edges->right) && positive(edges->bottom);
    }
    if (const auto* color = std::get_if<Color>(&value)) {
        const auto channel = [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; };
        return channel(color->r) && channel(color->g) && channel(color->b) && channel(color->a);
    }
    if (const auto* font = std::get_if<FontId>(&value)) { return font->valid(); }
    if (const auto* size = std::get_if<Size>(&value)) {
        return positive(size->width) && positive(size->height);
    }
    const auto& size = std::get<FontSize>(value);
    return !size.units || (std::isfinite(*size.units) && *size.units > 0 && *size.units <= 512);
}

bool matches(Property property, const ThemeValue& value) {
    switch (property) {
    case Property::fill: case Property::border_color: case Property::text_color:
    case Property::selection_color: case Property::caret_color: case Property::composition_color:
        return std::holds_alternative<Color>(value);
    case Property::border_width: case Property::corner_radius: case Property::spacing:
    case Property::indicator_gap: case Property::slider_track_thickness:
    case Property::slider_thumb_width: case Property::slider_thumb_inset:
    case Property::menu_leading_width: case Property::menu_trailing_width:
    case Property::menu_column_gap:
        return std::holds_alternative<float>(value);
    case Property::padding: return std::holds_alternative<Edges>(value);
    case Property::font: return std::holds_alternative<FontId>(value);
    case Property::min_size: case Property::indicator_size:
        return std::holds_alternative<Size>(value);
    case Property::font_size: return std::holds_alternative<FontSize>(value);
    case Property::count: return false;
    }
    return false;
}

Dirty affected(Property property) {
    return property == Property::padding || property == Property::spacing ||
                   property == Property::font || property == Property::font_size ||
                   property == Property::min_size || property == Property::indicator_size ||
                   property == Property::indicator_gap ||
                   property == Property::menu_leading_width ||
                   property == Property::menu_trailing_width ||
                   property == Property::menu_column_gap
               ? Dirty::measure : Dirty::shape;
}

} // namespace

void Context::mark_style(ElementId element, Property property) {
    mark(element, Dirty::style | affected(property));
}

base::Status Context::set_theme_value(std::string name, ThemeValue value) {
    if (name.empty() || !valid_value(value)) {
        return invalid_style("GUI theme requires a name and a finite, valid value");
    }
    const auto existing = theme_.find(name);
    if (existing != theme_.end()) {
        if (existing->second.index() != value.index()) {
            return invalid_style("GUI theme value types cannot change");
        }
        if (existing->second == value) { return base::Status::success(); }
    }
    theme_[name] = std::move(value);
    const auto dependents = theme_dependents_.find(name);
    if (dependents != theme_dependents_.end()) {
        for (const auto& dependent : dependents->second) {
            Node* node = find(dependent.element);
            if (node != nullptr &&
                !node->styles[static_cast<std::size_t>(dependent.property)].override_value) {
                mark_style(dependent.element, dependent.property);
            }
        }
    }
    return name.starts_with("gui.") || name.starts_with("dock.")
        ? refresh_component_styles() : base::Status::success();
}

base::Result<ThemeValue> Context::theme_value(std::string_view name) const {
    const auto found = theme_.find(std::string(name));
    if (found == theme_.end()) { return invalid_style("GUI theme name is not defined"); }
    return found->second;
}

base::Status Context::bind_property(ElementId element, Property property, std::string name) {
    Node* node = find(element);
    const auto found = theme_.find(name);
    if (node == nullptr || found == theme_.end() || !matches(property, found->second)) {
        return invalid_style("GUI property needs a live element and matching theme value");
    }
    if (property == Property::spacing &&
        dynamic_cast<const StackLayout*>(node->layout.get()) == nullptr) {
        return invalid_style("GUI spacing is a StackLayout property");
    }
    auto& binding = node->styles[static_cast<std::size_t>(property)];
    if (binding.name == name && !binding.component) { return base::Status::success(); }
    if (!binding.name.empty()) {
        auto& old = theme_dependents_[binding.name];
        std::erase_if(old, [element, property](const ThemeDependent& dependent) {
            return dependent.element == element && dependent.property == property;
        });
    }
    if (binding.component) {
        forget_component_binding(element, property, binding.component->owner);
        binding.component.reset();
    }
    binding.name = std::move(name);
    theme_dependents_[binding.name].push_back({element, property});
    if (!binding.override_value && binding.resolved != found->second) {
        mark_style(element, property);
    }
    return base::Status::success();
}

base::Status Context::set_property_override(ElementId element, Property property,
                                            ThemeValue value) {
    Node* node = find(element);
    if (node == nullptr || !matches(property, value) || !valid_value(value)) {
        return invalid_style("GUI override requires a live element and matching valid value");
    }
    if (property == Property::spacing &&
        dynamic_cast<const StackLayout*>(node->layout.get()) == nullptr) {
        return invalid_style("GUI spacing is a StackLayout property");
    }
    auto& binding = node->styles[static_cast<std::size_t>(property)];
    if (binding.override_value == value) { return base::Status::success(); }
    const bool padding_precedence_changed =
        property == Property::padding && !binding.override_value && node->params.padding != Edges{};
    binding.override_value = std::move(value);
    if (binding.resolved != binding.override_value || padding_precedence_changed) {
        mark_style(element, property);
    }
    if (node->component && node->component->kind == Component::checkbox &&
        (property == Property::indicator_size || property == Property::indicator_gap)) {
        return refresh_component_styles(element);
    }
    return base::Status::success();
}

base::Status Context::clear_property_override(ElementId element, Property property) {
    Node* node = find(element);
    if (node == nullptr || property >= Property::count) {
        return invalid_style("GUI override requires a live element and property");
    }
    auto& binding = node->styles[static_cast<std::size_t>(property)];
    if (!binding.override_value) { return base::Status::success(); }
    if (binding.name.empty() && !binding.component) {
        return invalid_style("GUI override has no bound theme value to restore");
    }
    const auto value = binding.component
        ? component_property(binding.component->owner, property, binding.component->part)
        : theme_value(binding.name);
    if (!value.ok()) { return value.status(); }
    const bool padding_precedence_changed =
        property == Property::padding && node->params.padding != Edges{};
    binding.override_value.reset();
    if (binding.resolved != value.value() || padding_precedence_changed) {
        mark_style(element, property);
    }
    if (node->component && node->component->kind == Component::checkbox &&
        (property == Property::indicator_size || property == Property::indicator_gap)) {
        return refresh_component_styles(element);
    }
    return base::Status::success();
}

base::Result<ThemeValue> Context::property(ElementId element, Property property) const {
    const Node* node = find(element);
    if (node == nullptr) { return invalid_style("GUI property element is not alive"); }
    switch (property) {
    case Property::fill: return ThemeValue{node->decoration.fill};
    case Property::border_color: return ThemeValue{node->decoration.border};
    case Property::border_width: return ThemeValue{node->decoration.border_width};
    case Property::corner_radius: return ThemeValue{node->decoration.corner_radius};
    case Property::padding: return ThemeValue{node->effective_params.padding};
    case Property::font: return ThemeValue{node->font};
    case Property::text_color:
        return ThemeValue{node->text_color};
    case Property::min_size: return ThemeValue{node->style_min};
    case Property::font_size: return ThemeValue{node->font_size};
    case Property::selection_color: return ThemeValue{node->selection_color};
    case Property::caret_color: return ThemeValue{node->caret_color};
    case Property::composition_color: return ThemeValue{node->composition_color};
    case Property::indicator_size: return ThemeValue{node->indicator_size};
    case Property::indicator_gap: return ThemeValue{node->indicator_gap};
    case Property::slider_track_thickness: return ThemeValue{node->slider_track_thickness};
    case Property::slider_thumb_width: return ThemeValue{node->slider_thumb_width};
    case Property::slider_thumb_inset: return ThemeValue{node->slider_thumb_inset};
    case Property::menu_leading_width: return ThemeValue{node->menu_leading_width};
    case Property::menu_trailing_width: return ThemeValue{node->menu_trailing_width};
    case Property::menu_column_gap: return ThemeValue{node->menu_column_gap};
    case Property::spacing:
        if (const auto* stack = dynamic_cast<const StackLayout*>(node->layout.get())) {
            return ThemeValue{stack->options().spacing};
        }
        return invalid_style("GUI spacing is a StackLayout property");
    case Property::count: return invalid_style("GUI property is out of range");
    }
    return invalid_style("GUI property is out of range");
}

base::Status Context::set_text_source(ElementId element, TextEngine& engine, FontId font,
                                      std::string text, TextureId atlas) {
    Node* node = find(element);
    if (node == nullptr) { return invalid_style("GUI text source element is not alive"); }
    FontId rendered = font;
    if (node->font_size.units) {
        const auto sized = engine.sized_font(font, *node->font_size.units);
        if (!sized.ok()) { return sized.status(); }
        rendered = sized.value();
    }
    if (node->text_source && node->text_source->engine == &engine &&
        node->text_source->text == text && node->font == font &&
        node->text_source->atlas == atlas && node->text_source->rendered_font == rendered) {
        return base::Status::success();
    }
    auto shaped = engine.shape(rendered, text);
    if (!shaped.ok()) { return shaped.status(); }
    const Color color = node->text_color;
    const auto status = set_text(element, std::move(shaped).value(), atlas, color);
    if (!status.ok()) { return status; }
    node = find(element);
    node->font = font;
    node->text_source = Node::TextSource{&engine, std::move(text), atlas, rendered};
    return base::Status::success();
}

base::Result<StyleStats> Context::update_styles() {
    StyleStats stats;
    const auto pending = dirty_elements();
    for (ElementId element : pending) {
        Node* node = find(element);
        if (node == nullptr || !has(node->dirty, Dirty::style)) { continue; }
        Decoration decoration = node->decoration;
        auto style_padding = node->style_padding;
        FontId font = node->font;
        FontSize font_size = node->font_size;
        Size style_min = node->style_min;
        Color text_color = node->text_color;
        Color selection_color = node->selection_color;
        Color caret_color = node->caret_color;
        Color composition_color = node->composition_color;
        Size indicator_size = node->indicator_size;
        float indicator_gap = node->indicator_gap;
        float slider_track_thickness = node->slider_track_thickness;
        float slider_thumb_width = node->slider_thumb_width;
        float slider_thumb_inset = node->slider_thumb_inset;
        float menu_leading_width = node->menu_leading_width;
        float menu_trailing_width = node->menu_trailing_width;
        float menu_column_gap = node->menu_column_gap;
        std::optional<float> spacing;
        std::array<std::optional<ThemeValue>, static_cast<std::size_t>(Property::count)> values;
        bool changed = false;
        for (std::size_t i = 0; i < node->styles.size(); ++i) {
            const auto& binding = node->styles[i];
            if (binding.name.empty() && !binding.component && !binding.override_value) { continue; }
            if (binding.override_value) {
                values[i] = *binding.override_value;
            } else {
                const auto resolved = binding.component
                    ? component_property(binding.component->owner, static_cast<Property>(i),
                                         binding.component->part)
                    : theme_value(binding.name);
                if (!resolved.ok()) { return resolved.status(); }
                values[i] = resolved.value();
            }
            const ThemeValue& value = *values[i];
            if (!matches(static_cast<Property>(i), value) || !valid_value(value)) {
                return invalid_style("GUI resolved a property with an invalid value");
            }
            if (binding.resolved == value) { continue; }
            changed = true;
            switch (static_cast<Property>(i)) {
            case Property::fill: decoration.fill = std::get<Color>(value); break;
            case Property::border_color: decoration.border = std::get<Color>(value); break;
            case Property::border_width: decoration.border_width = std::get<float>(value); break;
            case Property::corner_radius: decoration.corner_radius = std::get<float>(value); break;
            case Property::padding: style_padding = std::get<Edges>(value); break;
            case Property::spacing: spacing = std::get<float>(value); break;
            case Property::font: font = std::get<FontId>(value); break;
            case Property::text_color: text_color = std::get<Color>(value); break;
            case Property::min_size: style_min = std::get<Size>(value); break;
            case Property::font_size: font_size = std::get<FontSize>(value); break;
            case Property::selection_color: selection_color = std::get<Color>(value); break;
            case Property::caret_color: caret_color = std::get<Color>(value); break;
            case Property::composition_color: composition_color = std::get<Color>(value); break;
            case Property::indicator_size: indicator_size = std::get<Size>(value); break;
            case Property::indicator_gap: indicator_gap = std::get<float>(value); break;
            case Property::slider_track_thickness:
                slider_track_thickness = std::get<float>(value); break;
            case Property::slider_thumb_width: slider_thumb_width = std::get<float>(value); break;
            case Property::slider_thumb_inset: slider_thumb_inset = std::get<float>(value); break;
            case Property::menu_leading_width: menu_leading_width = std::get<float>(value); break;
            case Property::menu_trailing_width: menu_trailing_width = std::get<float>(value); break;
            case Property::menu_column_gap: menu_column_gap = std::get<float>(value); break;
            case Property::count: break;
            }
        }
        std::optional<TextLayout> shaped;
        FontId rendered;
        if (node->text_source) {
            rendered = font;
            if (font_size.units) {
                const auto sized = node->text_source->engine->sized_font(font, *font_size.units);
                if (!sized.ok()) { return sized.status(); }
                rendered = sized.value();
            }
            if (font != node->font || rendered != node->text_source->rendered_font) {
                auto result = node->text_source->engine->shape(rendered, node->text_source->text);
                if (!result.ok()) { return result.status(); }
                shaped = std::move(result).value();
            }
        }
        if (spacing) {
            const auto* stack = dynamic_cast<const StackLayout*>(node->layout.get());
            if (stack == nullptr) {
                return invalid_style("GUI spacing binding requires a StackLayout");
            }
            auto options = stack->options();
            if (options.spacing != *spacing) {
                options.spacing = *spacing;
                const auto status = set_layout(element, std::make_shared<StackLayout>(options));
                if (!status.ok()) { return status; }
            }
        }
        auto status = set_decoration(element, decoration);
        if (!status.ok()) { return status; }
        const auto before = node->effective_params;
        node->style_min = style_min;
        node->style_padding = style_padding;
        refresh_effective_params(*node);
        if (before != node->effective_params) { mark(element, Dirty::measure); }
        if (node->text && (shaped || text_color != node->text_color)) {
            auto source = node->text_source;
            status = set_text(element, shaped ? std::move(*shaped) : node->text->layout,
                              node->text->atlas, text_color);
            node = find(element);
            node->text_source = std::move(source);
            if (!status.ok()) { return status; }
        }
        if (node->text_source) { node->text_source->rendered_font = rendered; }
        node->font = font;
        node->font_size = font_size;
        node->text_color = text_color;
        node->selection_color = selection_color;
        node->caret_color = caret_color;
        node->composition_color = composition_color;
        node->indicator_size = indicator_size;
        node->indicator_gap = indicator_gap;
        node->slider_track_thickness = slider_track_thickness;
        node->slider_thumb_width = slider_thumb_width;
        node->slider_thumb_inset = slider_thumb_inset;
        node->menu_leading_width = menu_leading_width;
        node->menu_trailing_width = menu_trailing_width;
        node->menu_column_gap = menu_column_gap;
        for (std::size_t i = 0; i < node->styles.size(); ++i) {
            if (values[i]) { node->styles[i].resolved = std::move(values[i]); }
        }
        auto handler = node->component ? node->component->handler : std::function<base::Status()>{};
        node->dirty = static_cast<Dirty>(static_cast<std::uint8_t>(node->dirty) &
                                         ~static_cast<std::uint8_t>(Dirty::style));
        if (changed) {
            ++stats.resolved;
            if (handler) {
                status = handler();
                if (!status.ok()) {
                    if (Node* failed = find(element)) {
                        for (auto& binding : failed->styles) { binding.resolved.reset(); }
                        mark(element, Dirty::style);
                    }
                    return status;
                }
            }
        }
    }
    clear_dirty(Dirty::none);
    return stats;
}

void Context::forget_styles(ElementId element) {
    const Node* node = find(element);
    if (node == nullptr) { return; }
    for (std::size_t i = 0; i < node->styles.size(); ++i) {
        const auto& binding = node->styles[i];
        if (!binding.name.empty()) {
            const auto found = theme_dependents_.find(binding.name);
            if (found != theme_dependents_.end()) {
                std::erase_if(found->second, [element](const ThemeDependent& dependent) {
                    return dependent.element == element;
                });
                if (found->second.empty()) { theme_dependents_.erase(found); }
            }
        }
        if (binding.component) {
            forget_component_binding(element, static_cast<Property>(i), binding.component->owner);
        }
    }
    if (const auto found = component_dependents_.find(element.value());
        found != component_dependents_.end()) {
        for (const auto& entry : found->second) {
            if (Node* target = find(entry.element)) {
                auto& binding = target->styles[static_cast<std::size_t>(entry.property)];
                binding.component.reset();
                binding.resolved.reset();
            }
        }
        component_dependents_.erase(found);
    }
    if (node->component) {
        component_owners_[static_cast<std::size_t>(node->component->kind)].erase(element.value());
    }
}

} // namespace truffle::gui

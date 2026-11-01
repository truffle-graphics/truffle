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
    return std::get<FontId>(value).valid();
}

bool matches(Property property, const ThemeValue& value) {
    switch (property) {
    case Property::fill: case Property::border_color: case Property::text_color:
        return std::holds_alternative<Color>(value);
    case Property::border_width: case Property::corner_radius: case Property::spacing:
        return std::holds_alternative<float>(value);
    case Property::padding: return std::holds_alternative<Edges>(value);
    case Property::font: return std::holds_alternative<FontId>(value);
    case Property::count: return false;
    }
    return false;
}

Dirty affected(Property property) {
    return property == Property::padding || property == Property::spacing ||
                   property == Property::font
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
    return base::Status::success();
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
    if (binding.name == name) { return base::Status::success(); }
    if (!binding.name.empty()) {
        auto& old = theme_dependents_[binding.name];
        std::erase_if(old, [element, property](const ThemeDependent& dependent) {
            return dependent.element == element && dependent.property == property;
        });
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
    binding.override_value = std::move(value);
    if (binding.resolved != binding.override_value) { mark_style(element, property); }
    return base::Status::success();
}

base::Status Context::clear_property_override(ElementId element, Property property) {
    Node* node = find(element);
    if (node == nullptr || property >= Property::count) {
        return invalid_style("GUI override requires a live element and property");
    }
    auto& binding = node->styles[static_cast<std::size_t>(property)];
    if (!binding.override_value) { return base::Status::success(); }
    if (binding.name.empty()) {
        return invalid_style("GUI override has no bound theme value to restore");
    }
    binding.override_value.reset();
    if (binding.resolved != theme_.at(binding.name)) { mark_style(element, property); }
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
    case Property::padding: return ThemeValue{node->params.padding};
    case Property::font: return ThemeValue{node->font};
    case Property::text_color:
        return ThemeValue{node->text_color};
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
    if (node->text_source && node->text_source->engine == &engine &&
        node->text_source->text == text && node->font == font &&
        node->text_source->atlas == atlas) {
        return base::Status::success();
    }
    auto shaped = engine.shape(font, text);
    if (!shaped.ok()) { return shaped.status(); }
    const Color color = node->text_color;
    const auto status = set_text(element, std::move(shaped).value(), atlas, color);
    if (!status.ok()) { return status; }
    node = find(element);
    node->font = font;
    node->text_source = Node::TextSource{&engine, std::move(text), atlas};
    return base::Status::success();
}

base::Result<StyleStats> Context::update_styles() {
    StyleStats stats;
    // Setters can add ancestor dirt; don't retain iterators into dirty_queue_.
    const auto pending = dirty_elements();
    for (ElementId element : pending) {
        Node* node = find(element);
        if (node == nullptr || !has(node->dirty, Dirty::style)) { continue; }
        Decoration decoration = node->decoration;
        LayoutParams params = node->params;
        FontId font = node->font;
        Color text_color = node->text_color;
        std::optional<float> spacing;
        bool changed = false;
        for (std::size_t i = 0; i < node->styles.size(); ++i) {
            const auto& binding = node->styles[i];
            if (binding.name.empty() && !binding.override_value) { continue; }
            const ThemeValue& value = binding.override_value
                ? *binding.override_value : theme_.at(binding.name);
            if (binding.resolved == value) { continue; }
            changed = true;
            switch (static_cast<Property>(i)) {
            case Property::fill: decoration.fill = std::get<Color>(value); break;
            case Property::border_color: decoration.border = std::get<Color>(value); break;
            case Property::border_width: decoration.border_width = std::get<float>(value); break;
            case Property::corner_radius: decoration.corner_radius = std::get<float>(value); break;
            case Property::padding: params.padding = std::get<Edges>(value); break;
            case Property::spacing: spacing = std::get<float>(value); break;
            case Property::font: font = std::get<FontId>(value); break;
            case Property::text_color: text_color = std::get<Color>(value); break;
            case Property::count: break;
            }
        }
        std::optional<TextLayout> shaped;
        if (node->text_source && font != node->font) {
            auto result = node->text_source->engine->shape(font, node->text_source->text);
            if (!result.ok()) { return result.status(); }
            shaped = std::move(result).value();
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
        status = set_layout_params(element, params);
        if (!status.ok()) { return status; }
        node = find(element);
        if (node->text) {
            auto source = node->text_source;
            status = set_text(element, shaped ? std::move(*shaped) : node->text->layout,
                              node->text->atlas, text_color);
            node->text_source = std::move(source);
            if (!status.ok()) { return status; }
        }
        node->font = font;
        node->text_color = text_color;
        for (auto& binding : node->styles) {
            if (!binding.name.empty() || binding.override_value) {
                binding.resolved = binding.override_value
                    ? *binding.override_value : theme_.at(binding.name);
            }
        }
        node->dirty = static_cast<Dirty>(static_cast<std::uint8_t>(node->dirty) &
                                         ~static_cast<std::uint8_t>(Dirty::style));
        if (changed) { ++stats.resolved; }
    }
    clear_dirty(Dirty::style);
    return stats;
}

void Context::forget_styles(ElementId element) {
    const Node* node = find(element);
    if (node == nullptr) { return; }
    for (const auto& binding : node->styles) {
        if (binding.name.empty()) { continue; }
        const auto found = theme_dependents_.find(binding.name);
        if (found != theme_dependents_.end()) {
            std::erase_if(found->second, [element](const ThemeDependent& dependent) {
                return dependent.element == element;
            });
            if (found->second.empty()) { theme_dependents_.erase(found); }
        }
    }
}

} // namespace truffle::gui

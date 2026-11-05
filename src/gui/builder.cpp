#include "truffle/gui/builder.hpp"

#include <utility>

namespace truffle::gui {
namespace {

BuildNode container(WidgetKind kind, std::shared_ptr<const Layout> layout,
                    BuildChildren children) {
    BuildNode node;
    node.desc.kind = kind;
    node.layout = std::move(layout);
    node.children.assign(children.begin(), children.end());
    return node;
}

BuildNode stack(Axis axis, float spacing, BuildChildren children) {
    return container(WidgetKind::panel,
                     std::make_shared<StackLayout>(StackOptions{.axis = axis, .spacing = spacing}),
                     children);
}

void apply(LayoutParams& params, const BuildNode& node) {
    if (node.width_value) { params.width = node.width_value; }
    if (node.height_value) { params.height = node.height_value; }
    if (node.weight_value) { params.weight = *node.weight_value; }
    if (node.min_value) { params.min = *node.min_value; }
    if (node.position_value) { params.position = *node.position_value; }
    if (node.cell_value) { params.cell = *node.cell_value; }
}

class Builder {
public:
    Builder(Context& context, const BuildOptions& options) : context_(context), options_(options) {}

    base::Result<Widget> create(ElementId parent, const BuildNode& node) {
        WidgetDesc desc = node.desc;
        if (desc.text_engine == nullptr) {
            desc.text_engine = options_.text_engine;
            desc.font = options_.font;
            desc.atlas = options_.atlas;
        }
        auto widget = Widget::create(context_, parent, std::move(desc));
        if (!widget.ok()) { return widget.status(); }
        const auto status = configure(widget.value(), node);
        if (!status.ok()) {
            const auto cleanup = context_.destroy(widget.value().element());
            return cleanup.ok() ? status : cleanup;
        }
        return widget;
    }

    void clear_targets() noexcept {
        for (Widget* target : targets_) { *target = Widget{}; }
    }

private:
    base::Status configure(const Widget& widget, const BuildNode& node) {
        const auto id = widget.element();
        if (node.layout) {
            auto status = context_.set_layout(widget.content(), node.layout);
            if (!status.ok()) { return status; }
        }
        auto params = context_.layout_params(id);
        if (!params.ok()) { return params.status(); }
        apply(params.value(), node);
        auto status = context_.set_layout_params(id, params.value());
        if (!status.ok()) { return status; }
        if (node.padding_value) {
            status = context_.set_property_override(id, Property::padding, *node.padding_value);
            if (!status.ok()) { return status; }
        }
        if (!node.enabled) {
            status = Widget{widget}.set_enabled(false);
            if (!status.ok()) { return status; }
        }
        for (const auto& child : node.children) {
            auto created = create(widget.content(), child);
            if (!created.ok()) { return created.status(); }
        }
        if (node.target != nullptr) {
            *node.target = widget;
            targets_.push_back(node.target);
        }
        return base::Status::success();
    }

    Context& context_;
    const BuildOptions& options_;
    std::vector<Widget*> targets_;
};

} // namespace

namespace ui {

BuildNode panel(BuildChildren children) {
    return container(WidgetKind::panel, nullptr, children);
}

BuildNode row(float spacing, BuildChildren children) {
    return stack(Axis::horizontal, spacing, children);
}

BuildNode column(float spacing, BuildChildren children) {
    return stack(Axis::vertical, spacing, children);
}

BuildNode grid(GridOptions options, BuildChildren children) {
    return container(WidgetKind::panel, std::make_shared<GridLayout>(std::move(options)),
                     children);
}

BuildNode scroll(BuildChildren children) {
    return container(WidgetKind::scroll_area, nullptr, children);
}

BuildNode label(std::string text) {
    BuildNode node;
    node.desc.kind = WidgetKind::label;
    node.desc.text = std::move(text);
    return node;
}

BuildNode button(std::string text, std::function<void()> activated) {
    BuildNode node;
    node.desc.kind = WidgetKind::button;
    node.desc.text = std::move(text);
    node.desc.activated = std::move(activated);
    return node;
}

BuildNode checkbox(std::string text, bool checked, std::function<void(bool)> toggled) {
    BuildNode node;
    node.desc.kind = WidgetKind::checkbox;
    node.desc.text = std::move(text);
    node.desc.checked = checked;
    node.desc.toggled = std::move(toggled);
    return node;
}

BuildNode slider(float min, float max, float value, std::function<void(float)> value_changed) {
    BuildNode node;
    node.desc.kind = WidgetKind::slider;
    node.desc.min = min;
    node.desc.max = max;
    node.desc.value = value;
    node.desc.value_changed = std::move(value_changed);
    return node;
}

BuildNode image(TextureId texture, Size size) {
    BuildNode node;
    node.desc.kind = WidgetKind::image;
    node.desc.image = texture;
    node.desc.image_size = size;
    return node;
}

BuildNode text_edit(std::string text, bool multiline,
                    std::function<void(const std::string&)> changed) {
    BuildNode node;
    node.desc.kind = WidgetKind::text_edit;
    node.desc.text = std::move(text);
    node.desc.multiline = multiline;
    node.desc.text_changed = std::move(changed);
    return node;
}

} // namespace ui

base::Result<Widget> build(Context& context, ElementId parent, const BuildNode& node,
                           const BuildOptions& options) {
    Builder builder(context, options);
    auto result = builder.create(parent, node);
    if (!result.ok()) { builder.clear_targets(); }
    return result;
}

} // namespace truffle::gui

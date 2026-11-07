#include "truffle/gui/widgets.hpp"

#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace truffle::gui {
namespace {

base::Status invalid_widget() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI widget is stale or has the wrong kind");
}

class SliderBuilder final : public ShapeBuilder {
public:
    explicit SliderBuilder(float ratio) : ratio_(ratio) {}
    void build(ShapeWriter& out, Size size, const Decoration& decoration) const override {
        out.rect({0, 0, size.width, size.height}, decoration);
        const float radius = std::min(6.0F, size.width * 0.5F);
        const float travel = std::max(0.0F, size.width - 2 * radius);
        Decoration track;
        track.fill = decoration.border;
        track.fill.a *= 0.4F;
        track.corner_radius = 2;
        out.rect({radius, size.height * 0.5F - 2, travel, 4}, track);
        Decoration thumb;
        thumb.fill = decoration.border;
        thumb.corner_radius = radius;
        out.rect({ratio_ * travel, 2, 2 * radius, std::max(0.0F, size.height - 4)}, thumb);
    }
private:
    float ratio_;
};

class ScrollLayout final : public Layout {
public:
    Size measure(LayoutPass& pass, ElementId element, Constraints content) const override {
        Size desired;
        for (auto child : pass.children(element)) {
            const Size size = pass.measure(child, {{}, {unbounded, unbounded}});
            desired.width = std::max(desired.width, size.width);
            desired.height = std::max(desired.height, size.height);
        }
        return content.clamp(desired);
    }
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override {
        Size extent = content.size();
        for (auto child : pass.children(element)) {
            const Size size = pass.desired(child);
            extent.width = std::max(extent.width, size.width);
            extent.height = std::max(extent.height, size.height);
            pass.arrange(child, {content.x, content.y, std::max(size.width, content.width),
                                 std::max(size.height, content.height)});
        }
        const Point offset = pass.child_offset(element);
        pass.set_child_offset(element, {
            std::clamp(offset.x, content.width - extent.width, 0.0F),
            std::clamp(offset.y, content.height - extent.height, 0.0F)});
    }
};

bool inside(Context& context, ElementId hit, ElementId widget) {
    while (context.alive(hit)) {
        if (hit == widget) { return true; }
        const auto parent = context.parent(hit);
        if (!parent.ok()) { return false; }
        hit = parent.value();
    }
    return false;
}

} // namespace

struct Widget::State {
    Context* context = nullptr;
    ElementId element;
    ElementId content;
    ElementId mark;
    WidgetDesc desc;
    TextEdit editor;
    bool hover = false;
    bool focused = false;
    bool key_pressed = false;
    std::optional<std::uint32_t> pointer;
    Point offset;

    bool live() const { return context != nullptr && context->alive(element); }
    bool interactive() const {
        return desc.kind == WidgetKind::button || desc.kind == WidgetKind::checkbox ||
               desc.kind == WidgetKind::slider;
    }
    base::Status appearance() {
        if (!live()) { return invalid_widget(); }
        const auto interaction = context->interaction(element);
        if (!interaction.ok()) { return interaction.status(); }
        const char* fill = !interaction.value().enabled ? "gui.disabled" :
                           pointer || key_pressed ? "gui.pressed" :
                           hover ? "gui.hover" : "gui.control";
        auto status = context->bind_property(element, Property::fill, fill);
        if (!status.ok()) { return status; }
        if (desc.kind != WidgetKind::slider) {
            status = context->bind_property(element, Property::border_color,
                                             focused ? "gui.accent" : "gui.border");
        }
        return status;
    }
    base::Status check(bool value) {
        if (!live() || desc.kind != WidgetKind::checkbox) { return invalid_widget(); }
        if (desc.checked == value) { return base::Status::success(); }
        const auto status = context->bind_property(mark, Property::fill,
                                                    value ? "gui.accent" : "gui.control");
        if (status.ok()) { desc.checked = value; }
        return status;
    }
    base::Status number(float value) {
        if (!live() || desc.kind != WidgetKind::slider || !std::isfinite(value)) {
            return invalid_widget();
        }
        value = std::clamp(value, desc.min, desc.max);
        if (value == desc.value) { return base::Status::success(); }
        const auto status = context->set_shape_builder(element,
            std::make_shared<SliderBuilder>((value - desc.min) / (desc.max - desc.min)));
        if (status.ok()) { desc.value = value; }
        return status;
    }
    base::Status move_scroll(Point requested) {
        if (!live() || desc.kind != WidgetKind::scroll_area ||
            !std::isfinite(requested.x) || !std::isfinite(requested.y)) {
            return invalid_widget();
        }
        const auto outer = context->bounds(element);
        const auto inner = context->bounds(content);
        const auto params = context->layout_params(element);
        if (!outer.ok()) { return outer.status(); }
        if (!inner.ok()) { return inner.status(); }
        if (!params.ok()) { return params.status(); }
        const auto padding = params.value().padding;
        offset = {std::clamp(requested.x, 0.0F, std::max(0.0F, inner.value().width -
                   std::max(0.0F, outer.value().width - padding.horizontal()))),
                  std::clamp(requested.y, 0.0F, std::max(0.0F, inner.value().height -
                   std::max(0.0F, outer.value().height - padding.vertical())))};
        return context->set_child_offset(element, {-offset.x, -offset.y});
    }
    base::Status activate() {
        if (desc.kind == WidgetKind::checkbox) {
            const auto status = check(!desc.checked);
            if (!status.ok()) { return status; }
            const auto callback = desc.toggled;
            if (callback) { callback(desc.checked); }
        } else {
            const auto callback = desc.activated;
            if (callback) { callback(); }
        }
        return base::Status::success();
    }
    base::Status from_pointer(Point position) {
        const auto bounds = context->absolute_bounds(element);
        if (!bounds.ok()) { return bounds.status(); }
        const float radius = std::min(6.0F, bounds.value().width * 0.5F);
        const float travel = bounds.value().width - 2 * radius;
        const float ratio = travel > 0
            ? std::clamp((position.x - bounds.value().x - radius) / travel, 0.0F, 1.0F) : 0;
        const float previous = desc.value;
        const auto status = number(desc.min + ratio * (desc.max - desc.min));
        if (!status.ok()) { return status; }
        const auto callback = desc.value_changed;
        if (previous != desc.value && callback) { callback(desc.value); }
        return base::Status::success();
    }
    base::Status on_event(RoutedEvent& event) {
        if (!live()) { return base::Status::success(); }
        if (const auto* change = std::get_if<HoverEvent>(&event.payload)) {
            hover = change->entered;
            return interactive() ? appearance() : base::Status::success();
        }
        if (const auto* change = std::get_if<ElementFocusEvent>(&event.payload)) {
            focused = change->focused;
            if (!focused) { key_pressed = false; }
            return interactive() ? appearance() : base::Status::success();
        }
        if (const auto* lost = std::get_if<CaptureLostEvent>(&event.payload)) {
            if (pointer == lost->pointer) { pointer.reset(); }
            return interactive() ? appearance() : base::Status::success();
        }
        if (desc.kind == WidgetKind::scroll_area) {
            const Point translation = context->child_offset(element);
            offset = {-translation.x, -translation.y};
            Point requested = offset;
            if (const auto* wheel = event.input<WheelEvent>()) {
                const float unit = wheel->precise ? 1.0F : 20.0F;
                requested = {offset.x - wheel->delta_x * unit,
                             offset.y - wheel->delta_y * unit};
            } else if (const auto* key = event.input<KeyEvent>(); key && key->down) {
                switch (key->key) {
                case Key::down: requested.y += 20; break;
                case Key::up: requested.y -= 20; break;
                case Key::right: requested.x += 20; break;
                case Key::left: requested.x -= 20; break;
                case Key::page_down: requested.y += context->bounds(element).value().height; break;
                case Key::page_up: requested.y -= context->bounds(element).value().height; break;
                case Key::home: requested = {}; break;
                default: return base::Status::success();
                }
            } else { return base::Status::success(); }
            const Point previous = offset;
            const auto status = move_scroll(requested);
            if (offset != previous) {
                event.prevent_default();
                event.stop_propagation(); // at an edge the parent may scroll instead
            }
            return status;
        }
        if (!interactive()) { return base::Status::success(); }
        if (const auto* input = event.input<PointerEvent>()) {
            if (input->action == PointerAction::down &&
                input->button == PointerButton::primary && !pointer) {
                pointer = input->pointer;
                auto status = context->capture_pointer(element, *pointer);
                if (!status.ok()) { pointer.reset(); return status; }
                status = appearance();
                if (!status.ok()) { return status; }
                event.prevent_default();
                event.stop_propagation();
                if (desc.kind == WidgetKind::slider) { return from_pointer(input->position); }
            } else if (pointer == input->pointer) {
                if (input->action == PointerAction::move && desc.kind == WidgetKind::slider) {
                    return from_pointer(input->position);
                }
                if (input->action == PointerAction::up &&
                    input->button == PointerButton::primary) {
                    pointer.reset();
                    auto status = appearance();
                    if (!status.ok()) { return status; }
                    event.prevent_default();
                    event.stop_propagation();
                    if (desc.kind != WidgetKind::slider &&
                        inside(*context, context->hit_test(input->position), element)) {
                        return activate();
                    }
                }
            }
        } else if (const auto* key = event.input<KeyEvent>()) {
            if (desc.kind == WidgetKind::slider && key->down) {
                float requested = desc.value;
                switch (key->key) {
                case Key::left: case Key::down: requested -= desc.step; break;
                case Key::right: case Key::up: requested += desc.step; break;
                case Key::home: requested = desc.min; break;
                case Key::end: requested = desc.max; break;
                default: return base::Status::success();
                }
                const float previous = desc.value;
                auto status = number(requested);
                if (!status.ok()) { return status; }
                event.prevent_default();
                event.stop_propagation();
                const auto callback = desc.value_changed;
                if (desc.value != previous && callback) { callback(desc.value); }
            } else if (desc.kind != WidgetKind::slider && key->key == Key::space) {
                event.prevent_default();
                event.stop_propagation();
                if (key->down && !key->repeat) { key_pressed = true; }
                else if (!key->down && key_pressed) {
                    key_pressed = false;
                    auto status = appearance();
                    if (!status.ok()) { return status; }
                    return activate();
                }
                return appearance();
            } else if (desc.kind != WidgetKind::slider && key->key == Key::enter &&
                       key->down && !key->repeat) {
                event.prevent_default();
                event.stop_propagation();
                return activate();
            }
        }
        return base::Status::success();
    }
};

base::Result<Widget> Widget::create(Context& context, ElementId parent, WidgetDesc desc) {
    if (desc.kind > WidgetKind::text_edit || !context.alive(parent)) {
        return invalid_widget();
    }
    if (desc.kind == WidgetKind::slider &&
        (!std::isfinite(desc.min) || !std::isfinite(desc.max) || desc.max <= desc.min ||
         !std::isfinite(desc.value) || !std::isfinite(desc.step) || desc.step <= 0 ||
         !std::isfinite(desc.max - desc.min))) {
        return invalid_widget();
    }
    if (desc.kind == WidgetKind::image &&
        (!desc.image.valid() || !std::isfinite(desc.image_size.width) ||
         !std::isfinite(desc.image_size.height) || desc.image_size.width <= 0 ||
         desc.image_size.height <= 0)) { return invalid_widget(); }
    const bool draws_text = desc.kind == WidgetKind::label ||
                           desc.kind == WidgetKind::button || desc.kind == WidgetKind::checkbox;
    if (draws_text && !desc.text.empty() &&
        (desc.text_engine == nullptr || !desc.font.valid() || !desc.atlas.valid())) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI text widgets require an engine, font and atlas");
    }
    auto status = install_default_theme(context);
    if (!status.ok()) { return status; }
    if (desc.kind == WidgetKind::text_edit) {
        auto editor = TextEdit::create(context, parent, {
            .text = desc.text, .text_engine = desc.text_engine, .font = desc.font,
            .atlas = desc.atlas, .multiline = desc.multiline, .read_only = desc.read_only,
            .changed = desc.text_changed, .submitted = desc.activated});
        if (!editor.ok()) { return editor.status(); }
        auto state = std::make_shared<State>();
        state->context = &context;
        state->editor = std::move(editor).value();
        state->element = state->editor.element();
        state->content = state->element;
        state->desc = std::move(desc);
        return Widget{std::move(state)};
    }
    const auto created = context.create(parent);
    if (!created.ok()) { return created.status(); }
    auto state = std::make_shared<State>();
    state->context = &context;
    state->element = created.value();
    state->content = state->element;
    state->desc = std::move(desc);
    const auto setup = [&]() -> base::Status {
        const auto id = state->element;
        const auto kind = state->desc.kind;
        auto result = context.set_semantics(id, {
            static_cast<Context::Role>(static_cast<unsigned>(kind) + 1), state->desc.text});
        if (!result.ok()) { return result; }
        result = context.set_interaction(id, {
            .hit_test = kind != WidgetKind::label && kind != WidgetKind::image,
            .focusable = state->interactive() || kind == WidgetKind::scroll_area});
        if (!result.ok()) { return result; }
        LayoutParams params;
        if (state->interactive()) { params.min = {80, 32}; }
        if (kind == WidgetKind::slider) { params.min = {120, 24}; }
        if (kind == WidgetKind::image) {
            params.width = state->desc.image_size.width;
            params.height = state->desc.image_size.height;
            Decoration decoration{.fill = {1, 1, 1, 1}, .texture = state->desc.image};
            result = context.set_decoration(id, decoration);
            if (!result.ok()) { return result; }
        }
        result = context.set_layout_params(id, params);
        if (!result.ok()) { return result; }
        const auto bind = [&](Property property, const char* name) {
            return context.bind_property(id, property, name);
        };
        if (kind != WidgetKind::label) {
            result = bind(Property::fill, kind == WidgetKind::image ? "gui.image" :
                state->interactive() ? "gui.control" : "gui.surface");
            if (!result.ok()) { return result; }
            result = bind(Property::border_color,
                          kind == WidgetKind::slider ? "gui.accent" : "gui.border");
            if (!result.ok()) { return result; }
            result = bind(Property::corner_radius, "gui.radius");
            if (!result.ok()) { return result; }
            if (kind != WidgetKind::image) {
                result = bind(Property::border_width, "gui.border_width");
                if (!result.ok()) { return result; }
            }
        }
        if (kind == WidgetKind::panel || kind == WidgetKind::scroll_area ||
            kind == WidgetKind::button) {
            result = bind(Property::padding, "gui.padding");
            if (!result.ok()) { return result; }
        }
        if (kind == WidgetKind::checkbox) {
            result = bind(Property::padding, "gui.padding");
            if (!result.ok()) { return result; }
            result = context.set_property_override(id, Property::padding, Edges{32, 8, 8, 8});
            if (!result.ok()) { return result; }
            const auto mark = context.create(id);
            if (!mark.ok()) { return mark.status(); }
            state->mark = mark.value();
            LayoutParams mark_params;
            mark_params.width = 16.0F;
            mark_params.height = 16.0F;
            mark_params.position = {-24, 0};
            result = context.set_layout_params(state->mark, mark_params);
            if (!result.ok()) { return result; }
            result = context.set_interaction(state->mark, {.hit_test = false});
            if (!result.ok()) { return result; }
            result = context.bind_property(state->mark, Property::fill,
                       state->desc.checked ? "gui.accent" : "gui.control");
            if (!result.ok()) { return result; }
            result = context.bind_property(state->mark, Property::border_color, "gui.border");
            if (!result.ok()) { return result; }
            result = context.bind_property(state->mark, Property::border_width, "gui.border_width");
            if (!result.ok()) { return result; }
            result = context.bind_property(state->mark, Property::corner_radius, "gui.radius");
            if (!result.ok()) { return result; }
        }
        if (draws_text) {
            result = bind(Property::text_color, "gui.text");
            if (!result.ok()) { return result; }
            if (state->desc.text_engine != nullptr) {
                if (!context.theme_value("gui.font").ok()) {
                    result = context.set_theme_value("gui.font", state->desc.font);
                    if (!result.ok()) { return result; }
                }
                result = bind(Property::font, "gui.font");
                if (!result.ok()) { return result; }
                result = context.set_text_source(id, *state->desc.text_engine,
                    state->desc.font, state->desc.text, state->desc.atlas);
                if (!result.ok()) { return result; }
            }
        }
        if (kind == WidgetKind::slider) {
            state->desc.value = std::clamp(state->desc.value, state->desc.min, state->desc.max);
            result = context.set_shape_builder(id, std::make_shared<SliderBuilder>(
                (state->desc.value - state->desc.min) / (state->desc.max - state->desc.min)));
            if (!result.ok()) { return result; }
        }
        if (kind == WidgetKind::scroll_area) {
            const auto content = context.create(id);
            if (!content.ok()) { return content.status(); }
            state->content = content.value();
            result = context.set_interaction(state->content, {.hit_test = false});
            if (!result.ok()) { return result; }
            result = context.set_layout(id, std::make_shared<ScrollLayout>());
            if (!result.ok()) { return result; }
        }
        return context.set_event_handler(id, [state](Context&, RoutedEvent& event) {
            event.status = state->on_event(event);
        });
    };
    status = setup();
    if (!status.ok()) {
        const auto cleanup = context.destroy(state->element);
        if (!cleanup.ok()) { return cleanup; }
        return status;
    }
    return Widget{std::move(state)};
}

ElementId Widget::element() const noexcept { return state_ ? state_->element : ElementId{}; }
ElementId Widget::content() const noexcept { return state_ ? state_->content : ElementId{}; }
bool Widget::alive() const noexcept { return state_ && state_->live(); }

base::Status Widget::set_text(std::string text) {
    if (!alive()) { return invalid_widget(); }
    if (state_->desc.kind == WidgetKind::text_edit) {
        return state_->editor.set_text(std::move(text));
    }
    if (state_->desc.text == text) { return base::Status::success(); }
    const auto kind = state_->desc.kind;
    if (kind == WidgetKind::label || kind == WidgetKind::button || kind == WidgetKind::checkbox) {
        if (state_->desc.text_engine == nullptr) {
            return base::Status::failure(base::StatusCode::invalid_state,
                                         "GUI widget has no text engine");
        }
        const auto font = state_->context->property(element(), Property::font);
        if (!font.ok()) { return font.status(); }
        const auto status = state_->context->set_text_source(element(), *state_->desc.text_engine,
            std::get<FontId>(font.value()), text, state_->desc.atlas);
        if (!status.ok()) { return status; }
    }
    state_->desc.text = std::move(text);
    return state_->context->set_semantics(element(), {
        static_cast<Context::Role>(static_cast<unsigned>(kind) + 1), state_->desc.text});
}
base::Result<std::string> Widget::text() const {
    if (!alive()) { return invalid_widget(); }
    return state_->desc.kind == WidgetKind::text_edit
        ? state_->editor.text() : base::Result<std::string>{state_->desc.text};
}
base::Result<TextEdit> Widget::editor() const {
    if (!alive() || state_->desc.kind != WidgetKind::text_edit) { return invalid_widget(); }
    return state_->editor;
}
base::Status Widget::set_checked(bool checked) {
    return state_ ? state_->check(checked) : invalid_widget();
}
base::Result<bool> Widget::checked() const {
    if (!alive() || state_->desc.kind != WidgetKind::checkbox) { return invalid_widget(); }
    return state_->desc.checked;
}
base::Status Widget::set_value(float value) {
    return state_ ? state_->number(value) : invalid_widget();
}
base::Result<float> Widget::value() const {
    if (!alive() || state_->desc.kind != WidgetKind::slider) { return invalid_widget(); }
    return state_->desc.value;
}
base::Status Widget::set_scroll(Point offset) {
    return state_ ? state_->move_scroll(offset) : invalid_widget();
}
base::Result<Point> Widget::scroll() const {
    if (!alive() || state_->desc.kind != WidgetKind::scroll_area) { return invalid_widget(); }
    const Point offset = state_->context->child_offset(element());
    return Point{-offset.x, -offset.y};
}
base::Status Widget::set_enabled(bool enabled) {
    if (!alive()) { return invalid_widget(); }
    auto interaction = state_->context->interaction(element());
    if (!interaction.ok()) { return interaction.status(); }
    interaction.value().enabled = enabled;
    const auto status = state_->context->set_interaction(element(), interaction.value());
    if (!status.ok()) { return status; }
    return state_->interactive() ? state_->appearance() : base::Status::success();
}

} // namespace truffle::gui

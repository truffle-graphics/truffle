#include "truffle/gui/text_edit.hpp"

#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace truffle::gui {
namespace {

base::Status invalid(std::string message = "GUI text editor is not alive") {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

TextRange ordered(TextSelection selection) {
    return {std::min(selection.anchor, selection.caret),
            std::max(selection.anchor, selection.caret)};
}

std::string normalize(std::string_view text, bool multiline) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') { ++i; }
        result += c == '\r' || c == '\n' ? (multiline ? '\n' : ' ') : c;
    }
    return result;
}

Color token(const Context& context, std::string_view name) {
    const auto value = context.theme_value(name);
    const Color* color = value.ok() ? std::get_if<Color>(&value.value()) : nullptr;
    return color != nullptr ? *color : Color{1, 1, 1, 1};
}

// Colours are read while building, so a theme change that repaints the label
// (it binds gui.text) also refreshes selection, caret and composition marks.
class EditShapes final : public ShapeBuilder {
public:
    const Context* context = nullptr;
    std::vector<Rect> selection;
    std::vector<Rect> marked;
    std::optional<Rect> caret;

    void build(ShapeWriter& out, Size, const Decoration&) const override {
        for (const auto rect : selection) {
            out.rect(rect, {.fill = token(*context, "gui.selection")});
        }
        for (auto rect : marked) {
            rect.y += std::max(0.0F, rect.height - 1);
            rect.height = 1;
            out.rect(rect, {.fill = token(*context, "gui.composition")});
        }
        if (caret) { out.rect(*caret, {.fill = token(*context, "gui.caret")}); }
    }
};

} // namespace

struct TextEdit::State {
    struct Snapshot {
        std::string text;
        TextSelection selection;
    };

    Context* context = nullptr;
    ElementId root;
    ElementId label;
    TextEditDesc desc;
    TextSelection selection;
    std::vector<std::size_t> boundaries;
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    std::optional<CompositionEvent> composition;
    TextRange replaced;
    std::string displayed;
    FontId shaped_font;
    TextLayout layout;
    std::size_t visual = 0;
    std::optional<std::uint32_t> pointer;
    bool focused = false;
    bool scrolling_to_caret = true;

    bool live() const noexcept { return context != nullptr && context->alive(root); }
    bool boundary(std::size_t byte) const {
        return std::ranges::binary_search(boundaries, byte);
    }
    std::size_t snap(std::size_t byte) const {
        const auto it = std::upper_bound(boundaries.begin(), boundaries.end(), byte);
        return it == boundaries.begin() ? 0 : *std::prev(it);
    }
    std::size_t display_byte() const {
        return composition ? replaced.begin + composition->selection_end : selection.caret;
    }
    std::size_t visual_index() const {
        if (visual < layout.carets.size() && layout.carets[visual].byte == display_byte()) {
            return visual;
        }
        const auto it = std::ranges::find(layout.carets, display_byte(), &TextCaret::byte);
        if (it != layout.carets.end()) {
            return static_cast<std::size_t>(it - layout.carets.begin());
        }
        std::size_t nearest = 0;
        const auto distance = [&](std::size_t byte) {
            const auto target = display_byte();
            return byte > target ? byte - target : target - byte;
        };
        for (std::size_t i = 1; i < layout.carets.size(); ++i) {
            if (distance(layout.carets[i].byte) < distance(layout.carets[nearest].byte)) {
                nearest = i;
            }
        }
        return nearest;
    }
    Point caret_position() const {
        return layout.carets.empty() ? Point{} : layout.carets[visual_index()].position;
    }
    float caret_height() const {
        return layout.carets.empty() ? layout.line_height :
            layout.carets[visual_index()].height;
    }
    void publish() const {
        if (!focused || !live()) { return; }
        const auto range = composition
            ? TextRange{replaced.begin + composition->selection_begin,
                        replaced.begin + composition->selection_end}
            : ordered(selection);
        context->host().set_text_input_state(TextInputState{
            displayed, range,
            composition ? std::optional<TextRange>{{replaced.begin,
                replaced.begin + composition->preedit.size()}} : std::nullopt});
        const auto bounds = context->absolute_bounds(label);
        if (bounds.ok()) {
            const auto p = caret_position();
            context->host().set_text_input_area(
                Rect{bounds.value().x + p.x, bounds.value().y + p.y, 1, caret_height()});
        }
    }
    base::Status render() {
        std::string text = desc.text;
        if (composition) {
            text.replace(replaced.begin, replaced.end - replaced.begin, composition->preedit);
        }
        if (text != displayed || shaped_font != desc.font) {
            auto shaped = desc.text_engine->shape(desc.font, text);
            if (!shaped.ok()) { return shaped.status(); }
            const auto color = context->property(label, Property::text_color);
            if (!color.ok()) { return color.status(); }
            auto status = context->set_text(label, shaped.value(), desc.atlas,
                                            std::get<Color>(color.value()));
            if (!status.ok()) { return status; }
            layout = std::move(shaped).value();
            displayed = std::move(text);
            shaped_font = desc.font;
        }
        auto shapes = std::make_shared<EditShapes>();
        shapes->context = context;
        const auto range = composition
            ? TextRange{replaced.begin + composition->selection_begin,
                        replaced.begin + composition->selection_end}
            : ordered(selection);
        shapes->selection = layout.selection_rects(range.begin, range.end);
        if (composition) {
            shapes->marked = layout.selection_rects(replaced.begin,
                                                    replaced.begin + composition->preedit.size());
        }
        if (focused && !desc.read_only) {
            const auto p = caret_position();
            shapes->caret = Rect{p.x, p.y, 1, caret_height()};
        }
        auto status = context->set_shape_builder(label, std::move(shapes));
        if (status.ok()) { status = context->invalidate(root, Dirty::arrange); }
        if (status.ok()) { publish(); }
        return status;
    }
    static void push(std::vector<Snapshot>& stack, Snapshot snapshot) {
        if (stack.size() == 128) { stack.erase(stack.begin()); }
        stack.push_back(std::move(snapshot));
    }
    base::Status assign(std::string text, TextSelection next, bool history, bool notify) {
        if (text == desc.text && next == selection && !composition) {
            return base::Status::success();
        }
        auto split = text_graphemes(text);
        if (!split.ok()) { return split.status(); }
        const auto valid = [&](std::size_t byte) {
            return std::ranges::binary_search(split.value(), byte);
        };
        if (!valid(next.anchor) || !valid(next.caret)) {
            return invalid("text selection must use UTF-8 grapheme boundaries");
        }
        const Snapshot before{desc.text, selection};
        const auto old_boundaries = boundaries;
        const auto old_composition = composition;
        const bool changed = text != desc.text;
        desc.text = std::move(text);
        selection = next;
        boundaries = std::move(split).value();
        composition.reset();
        visual = layout.carets.size();
        scrolling_to_caret = true;
        auto status = render();
        if (!status.ok()) {
            desc.text = before.text;
            selection = before.selection;
            boundaries = old_boundaries;
            composition = old_composition;
            return status;
        }
        if (changed && history) {
            push(undo_stack, before);
            redo_stack.clear();
        }
        if (changed && notify && desc.changed) {
            const auto callback = desc.changed;
            const std::string value = desc.text;
            callback(value);
        }
        return status;
    }
    base::Status replace(std::string_view insertion, std::optional<TextRange> range = std::nullopt) {
        if (desc.read_only) { return base::Status::success(); }
        const auto target = range.value_or(composition ? replaced : ordered(selection));
        if (target.begin > target.end || !boundary(target.begin) || !boundary(target.end)) {
            return invalid("text replacement must use an ordered grapheme range");
        }
        const auto text = normalize(insertion, desc.multiline);
        auto valid = text_graphemes(text);
        if (!valid.ok()) { return valid.status(); }
        std::string candidate = desc.text;
        candidate.replace(target.begin, target.end - target.begin, text);
        // Inserting a combining sequence may join the adjacent grapheme.
        auto split = text_graphemes(candidate);
        if (!split.ok()) { return split.status(); }
        const auto after = std::lower_bound(split.value().begin(), split.value().end(),
                                            target.begin + text.size());
        const std::size_t caret = after == split.value().end() ? candidate.size() : *after;
        return assign(std::move(candidate), {caret, caret}, true, true);
    }
    base::Result<TextRange> model_range(TextRange range) const {
        if (range.begin > range.end || range.end > displayed.size()) {
            return invalid("input-method replacement range is outside the displayed text");
        }
        if (composition) {
            const auto end = replaced.begin + composition->preedit.size();
            const auto map = [&](std::size_t byte, bool last) {
                if (byte <= replaced.begin) { return byte; }
                if (byte < end) { return last ? replaced.end : replaced.begin; }
                return replaced.end + (byte - end);
            };
            range = {map(range.begin, false), map(range.end, true)};
        }
        if (!boundary(range.begin) || !boundary(range.end)) {
            return invalid("input-method replacement splits a grapheme");
        }
        return range;
    }
    base::Status preedit(const CompositionEvent& event) {
        if (desc.read_only) { return base::Status::success(); }
        if (event.preedit.empty()) {
            composition.reset();
            return render();
        }
        auto split = text_graphemes(event.preedit);
        if (!split.ok()) { return split.status(); }
        if (event.selection_begin > event.selection_end ||
            event.selection_end > event.preedit.size()) {
            return invalid("input-method selection is outside its preedit");
        }
        // Native IMEs may select a codepoint inside a combining grapheme.
        const auto codepoint_boundary = [&](std::size_t byte) {
            return byte == event.preedit.size() ||
                (static_cast<unsigned char>(event.preedit[byte]) & 0xC0U) != 0x80U;
        };
        if (!codepoint_boundary(event.selection_begin) ||
            !codepoint_boundary(event.selection_end)) {
            return invalid("input-method selection splits UTF-8");
        }
        TextRange target = composition ? replaced : ordered(selection);
        if (event.replacement) {
            auto mapped = model_range(*event.replacement);
            if (!mapped.ok()) { return mapped.status(); }
            target = mapped.value();
        }
        const auto previous = composition;
        const auto before = replaced;
        composition = event;
        replaced = target;
        scrolling_to_caret = true;
        auto status = render();
        if (!status.ok()) { composition = previous; replaced = before; }
        return status;
    }
    base::Status history(bool redo) {
        if (desc.read_only) { return base::Status::success(); }
        auto& source = redo ? redo_stack : undo_stack;
        auto& target = redo ? undo_stack : redo_stack;
        if (source.empty()) { return base::Status::success(); }
        const Snapshot before{desc.text, selection};
        const Snapshot next = source.back();
        // Notify only after both history stacks have reached their new state.
        const auto status = assign(next.text, next.selection, false, false);
        if (!status.ok()) { return status; }
        source.pop_back();
        push(target, before);
        if (desc.changed) {
            const auto callback = desc.changed;
            const auto text = desc.text;
            callback(text);
        }
        return status;
    }
    base::Status select(TextSelection next, std::optional<std::size_t> index = std::nullopt) {
        if (!boundary(next.anchor) || !boundary(next.caret)) {
            return invalid("text selection must use UTF-8 grapheme boundaries");
        }
        if (selection == next && !composition &&
            (!index || *index == visual_index())) { return base::Status::success(); }
        selection = next;
        composition.reset();
        visual = index.value_or(layout.carets.size());
        scrolling_to_caret = true;
        return render();
    }
    base::Status move(Key key, bool extend) {
        if (composition) {
            composition.reset();
            auto status = render();
            if (!status.ok()) { return status; }
        }
        if (layout.carets.empty()) { return base::Status::success(); }
        std::size_t index = visual_index();
        const auto p = layout.carets[index].position;
        if (key == Key::left || key == Key::right) {
            if (!extend && selection.anchor != selection.caret) {
                const auto anchor = std::ranges::find(layout.carets, selection.anchor,
                                                       &TextCaret::byte);
                const auto other = anchor == layout.carets.end() ? index :
                    static_cast<std::size_t>(anchor - layout.carets.begin());
                index = key == Key::left ? std::min(index, other) : std::max(index, other);
            } else {
                if (key == Key::left && index > 0) { --index; }
                if (key == Key::right && index + 1 < layout.carets.size()) { ++index; }
            }
        } else if (key == Key::home || key == Key::end) {
            if (key == Key::home) {
                while (index > 0 && layout.carets[index - 1].position.y == p.y) { --index; }
            } else {
                while (index + 1 < layout.carets.size() &&
                       layout.carets[index + 1].position.y == p.y) { ++index; }
            }
        } else {
            Point target = p;
            if (key == Key::up) { target.y -= layout.line_height; }
            if (key == Key::down) { target.y += layout.line_height; }
            const auto byte = layout.hit_test(target);
            const auto it = std::ranges::find(layout.carets, byte, &TextCaret::byte);
            if (it != layout.carets.end()) {
                index = static_cast<std::size_t>(it - layout.carets.begin());
            }
        }
        const auto byte = layout.carets[index].byte;
        return select({extend ? selection.anchor : byte, byte}, index);
    }
    base::Status key(const KeyEvent& event, RoutedEvent& routed) {
        if (!event.down) { return base::Status::success(); }
        const bool shortcut = has(event.modifiers, Modifiers::control) ||
                              has(event.modifiers, Modifiers::super);
        const bool shift = has(event.modifiers, Modifiers::shift);
        if (shortcut) {
            const auto range = ordered(selection);
            switch (event.key) {
            case Key::a:
                routed.prevent_default();
                return select({0, desc.text.size()});
            case Key::c:
            case Key::x:
                routed.prevent_default();
                if (range.begin != range.end) {
                    context->host().set_clipboard_text(
                        std::string_view(desc.text).substr(range.begin, range.end - range.begin));
                    if (event.key == Key::x) { return replace(""); }
                }
                return base::Status::success();
            case Key::v:
                routed.prevent_default();
                return replace(context->host().clipboard_text());
            case Key::z:
                routed.prevent_default();
                return history(shift);
            case Key::y:
                routed.prevent_default();
                return history(true);
            default: break;
            }
        }
        switch (event.key) {
        case Key::left: case Key::right: case Key::up: case Key::down:
        case Key::home: case Key::end:
            routed.prevent_default();
            return move(event.key, shift);
        case Key::backspace: case Key::delete_forward: {
            routed.prevent_default();
            if (composition) { composition.reset(); return render(); }
            TextRange range = ordered(selection);
            if (range.begin == range.end) {
                const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), range.begin);
                if (event.key == Key::backspace && it != boundaries.begin()) {
                    range.begin = *std::prev(it);
                } else if (event.key == Key::delete_forward &&
                           it != boundaries.end() && std::next(it) != boundaries.end()) {
                    range.end = *std::next(it);
                }
            }
            return replace("", range);
        }
        case Key::enter:
            routed.prevent_default();
            if (desc.multiline) { return replace("\n"); }
            if (desc.submitted) { const auto callback = desc.submitted; callback(); }
            return base::Status::success();
        case Key::escape:
            if (composition) {
                routed.prevent_default();
                composition.reset();
                return render();
            }
            return base::Status::success();
        default: return base::Status::success();
        }
    }
    base::Status on_event(RoutedEvent& event) {
        if (!live()) { return base::Status::success(); }
        if (const auto* hover = std::get_if<HoverEvent>(&event.payload)) {
            context->host().set_cursor(hover->entered ? Cursor::text : Cursor::arrow);
        } else if (const auto* focus = std::get_if<ElementFocusEvent>(&event.payload)) {
            focused = focus->focused;
            if (!focused) {
                composition.reset();
                pointer.reset();
                context->host().set_text_input_area(std::nullopt);
                context->host().set_text_input_state(std::nullopt);
            }
            auto status = context->bind_property(root, Property::border_color,
                                                 focused ? "gui.accent" : "gui.border");
            return status.ok() ? render() : status;
        } else if (std::holds_alternative<CaptureLostEvent>(event.payload)) {
            pointer.reset();
        } else if (const auto* input = event.input<PointerEvent>()) {
            const bool down = input->action == PointerAction::down &&
                              input->button == PointerButton::primary;
            if (down || (pointer == input->pointer && input->action == PointerAction::move)) {
                const auto bounds = context->absolute_bounds(label);
                if (!bounds.ok()) { return bounds.status(); }
                if (composition) {
                    composition.reset();
                    auto status = render();
                    if (!status.ok()) { return status; }
                }
                const auto byte = snap(layout.hit_test(
                    {input->position.x - bounds.value().x, input->position.y - bounds.value().y}));
                std::optional<std::size_t> affinity;
                double nearest = 0;
                for (std::size_t i = 0; i < layout.carets.size(); ++i) {
                    if (layout.carets[i].byte != byte) { continue; }
                    const auto p = layout.carets[i].position;
                    const double dx = static_cast<double>(input->position.x) - bounds.value().x - p.x;
                    const double dy = static_cast<double>(input->position.y) - bounds.value().y - p.y;
                    const double distance = dx * dx + dy * dy;
                    if (!affinity || distance < nearest) {
                        affinity = i;
                        nearest = distance;
                    }
                }
                const bool extend = !down || has(input->modifiers, Modifiers::shift);
                auto status = select({extend ? selection.anchor : byte, byte}, affinity);
                if (down && status.ok()) {
                    pointer = input->pointer;
                    status = context->capture_pointer(root, input->pointer);
                }
                event.prevent_default();
                event.stop_propagation();
                return status;
            }
        } else if (const auto* input = event.input<TextEvent>()) {
            event.prevent_default();
            event.stop_propagation();
            std::optional<TextRange> range;
            if (input->replacement) {
                auto mapped = model_range(*input->replacement);
                if (!mapped.ok()) { return mapped.status(); }
                range = mapped.value();
            }
            return replace(input->text, range);
        } else if (const auto* input = event.input<CompositionEvent>()) {
            event.prevent_default();
            event.stop_propagation();
            return preedit(*input);
        } else if (const auto* input = event.input<KeyEvent>()) {
            const auto status = key(*input, event);
            if (event.handled) { event.stop_propagation(); }
            return status;
        }
        return base::Status::success();
    }
};

namespace {

class EditLayout final : public Layout {
public:
    explicit EditLayout(std::shared_ptr<TextEdit::State> state) : state_(std::move(state)) {}
    Size measure(LayoutPass& pass, ElementId, Constraints content) const override {
        const auto text = pass.measure(state_->label, {{}, {unbounded, unbounded}});
        return content.clamp({std::min(240.0F, text.width), text.height});
    }
    void arrange(LayoutPass& pass, ElementId element, Rect content) const override {
        const auto size = pass.desired(state_->label);
        pass.arrange(state_->label, {content.x, content.y,
            std::max(content.width, size.width + 1), std::max(content.height, size.height)});
        auto offset = pass.child_offset(element);
        if (state_->scrolling_to_caret) {
            const auto caret = state_->caret_position();
            if (caret.x + offset.x < 0) { offset.x = -caret.x; }
            if (caret.x + 1 + offset.x > content.width) {
                offset.x = content.width - caret.x - 1;
            }
            if (caret.y + offset.y < 0) { offset.y = -caret.y; }
            if (caret.y + state_->caret_height() + offset.y > content.height) {
                offset.y = content.height - caret.y - state_->caret_height();
            }
            state_->scrolling_to_caret = false;
        }
        offset.x = std::clamp(offset.x, std::min(0.0F, content.width - size.width - 1), 0.0F);
        offset.y = std::clamp(offset.y, std::min(0.0F, content.height - size.height), 0.0F);
        pass.set_child_offset(element, offset);
        state_->publish();
    }
private:
    std::shared_ptr<TextEdit::State> state_;
};

} // namespace

base::Result<TextEdit> TextEdit::create(Context& context, ElementId parent, TextEditDesc desc) {
    if (desc.text_engine == nullptr || !desc.font.valid() || !desc.atlas.valid()) {
        return invalid("a text editor requires a text engine, font and registered atlas");
    }
    desc.text = normalize(desc.text, desc.multiline);
    auto boundaries = text_graphemes(desc.text);
    if (!boundaries.ok()) { return boundaries.status(); }
    auto root = context.create(parent);
    if (!root.ok()) { return root.status(); }
    auto state = std::make_shared<State>();
    state->context = &context;
    state->root = root.value();
    state->desc = std::move(desc);
    state->boundaries = std::move(boundaries).value();
    const auto setup = [&]() -> base::Status {
        if (auto status = install_default_theme(context); !status.ok()) { return status; }
        auto label = context.create(state->root);
        if (!label.ok()) { return label.status(); }
        state->label = label.value();
        auto status = context.set_interaction(state->label, {.hit_test = false});
        if (status.ok()) {
            status = context.set_interaction(state->root, {.focusable = true});
        }
        if (status.ok()) {
            LayoutParams params;
            params.min = {120, state->desc.multiline ? 80.0F : 30.0F};
            params.padding = {8, 6, 8, 6};
            status = context.set_layout_params(state->root, params);
        }
        if (status.ok()) {
            const std::pair<Property, const char*> bindings[] = {
                {Property::fill, "gui.surface"}, {Property::border_color, "gui.border"},
                {Property::border_width, "gui.border_width"},
                {Property::corner_radius, "gui.radius"}};
            for (const auto& [property, name] : bindings) {
                if (status.ok()) { status = context.bind_property(state->root, property, name); }
            }
        }
        if (status.ok()) {
            status = context.bind_property(state->label, Property::text_color, "gui.text");
        }
        if (status.ok()) {
            status = context.set_semantics(state->root, {Context::Role::text_edit, "Text editor"});
        }
        if (status.ok()) {
            status = context.set_layout(state->root, std::make_shared<EditLayout>(state));
        }
        if (status.ok()) { status = state->render(); }
        if (status.ok()) {
            status = context.set_event_handler(state->root, [state](Context&, RoutedEvent& event) {
                event.status = state->on_event(event);
            });
        }
        return status;
    };
    auto status = setup();
    if (!status.ok()) {
        const auto cleanup = context.destroy(root.value());
        return cleanup.ok() ? status : cleanup;
    }
    return TextEdit(std::move(state));
}

ElementId TextEdit::element() const noexcept { return state_ ? state_->root : ElementId{}; }
bool TextEdit::alive() const noexcept { return state_ && state_->live(); }
base::Result<std::string> TextEdit::text() const {
    return alive() ? base::Result<std::string>{state_->desc.text} : invalid();
}
base::Status TextEdit::set_text(std::string text) {
    if (!alive()) { return invalid(); }
    text = normalize(text, state_->desc.multiline);
    if (text == state_->desc.text && !state_->composition) { return base::Status::success(); }
    const auto size = text.size();
    auto status = state_->assign(std::move(text), {size, size}, false, false);
    if (status.ok()) { state_->undo_stack.clear(); state_->redo_stack.clear(); }
    return status;
}
base::Result<TextSelection> TextEdit::selection() const {
    return alive() ? base::Result<TextSelection>{state_->selection} : invalid();
}
base::Result<Rect> TextEdit::caret_rect() const {
    if (!alive()) { return invalid(); }
    const auto bounds = state_->context->absolute_bounds(state_->label);
    if (!bounds.ok()) { return bounds.status(); }
    const auto p = state_->caret_position();
    return Rect{bounds.value().x + p.x, bounds.value().y + p.y, 1, state_->caret_height()};
}
base::Status TextEdit::select(TextSelection selection) {
    return alive() ? state_->select(selection) : invalid();
}
base::Status TextEdit::set_read_only(bool read_only) {
    if (!alive()) { return invalid(); }
    if (state_->desc.read_only == read_only) { return base::Status::success(); }
    state_->desc.read_only = read_only;
    if (read_only) { state_->composition.reset(); }
    return state_->render();
}
base::Status TextEdit::set_font(FontId font) {
    if (!alive()) { return invalid(); }
    const auto before = state_->desc.font;
    state_->desc.font = font;
    auto status = state_->render();
    if (!status.ok()) { state_->desc.font = before; }
    return status;
}
base::Status TextEdit::undo() { return alive() ? state_->history(false) : invalid(); }
base::Status TextEdit::redo() { return alive() ? state_->history(true) : invalid(); }

} // namespace truffle::gui

#include "truffle/gui/region.hpp"

#include "widget_object.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace truffle::gui {
namespace {

enum class Kind : std::uint8_t { label, button, checkbox, slider, text_edit, row, column };

// User actions recorded by widget callbacks until the next run reads them.
struct Pending {
    int clicks = 0;
    std::optional<bool> checked;
    std::optional<float> value;
    std::optional<std::string> text;
};

struct Entry;

struct Level {
    ElementId element;
    std::unordered_map<std::string, std::unique_ptr<Entry>> entries;
    std::size_t next = 0;
};

struct Entry {
    Kind kind = Kind::label;
    ElementId element;
    detail::Widget widget;
    std::string text;
    float min = 0;
    float max = 0;
    float step = 0; // slider step, or container spacing
    std::shared_ptr<Pending> pending = std::make_shared<Pending>();
    Level children;
    std::uint64_t seen = 0;
};

[[nodiscard]] base::Status scope_error(const char* message) {
    return base::Status::failure(base::StatusCode::invalid_argument, message);
}

std::shared_ptr<const Layout> stack(Kind kind, float spacing) {
    return std::make_shared<StackLayout>(StackOptions{
        .axis = kind == Kind::row ? Axis::horizontal : Axis::vertical, .spacing = spacing});
}

} // namespace

struct RegionScope::State {
    Context& context;
    ElementId region;
    Region::Body body;
    TextStyle text;
    Level root;
    Level* level = &root;
    std::uint64_t run = 0;
    base::Status error = base::Status::success();
    ElementId last;

    State(Context& c, ElementId element, Region::Body b, TextStyle t)
        : context(c), region(element), body(std::move(b)), text(t) {
        root.element = element;
    }

    void fail(base::Status status) {
        if (error.ok()) { error = std::move(status); }
    }

    [[nodiscard]] bool alive(ElementId element) const { return context.parent(element).ok(); }

    detail::WidgetDesc desc(detail::WidgetKind kind, std::string_view label) const {
        detail::WidgetDesc result;
        result.kind = kind;
        result.text = std::string(label);
        result.text_engine = text.engine;
        result.font = text.font;
        result.atlas = text.atlas;
        return result;
    }

    // Finds or creates the item `id` in the current container and moves it to
    // the next position. Returns null after an error.
    template <typename Reusable, typename Make>
    Entry* enter(std::string_view id, Kind kind, Reusable reusable, Make make) {
        if (!error.ok()) { return nullptr; }
        const std::string key(id);
        auto found = level->entries.find(key);
        if (found != level->entries.end()) {
            Entry& old = *found->second;
            if (old.seen == run) {
                fail(scope_error("GUI region item id is repeated in its container"));
                return nullptr;
            }
            if (old.kind != kind || !reusable(old) || !alive(old.element)) {
                if (alive(old.element)) {
                    auto status = context.destroy(old.element);
                    if (!status.ok()) { fail(std::move(status)); return nullptr; }
                }
                level->entries.erase(found);
                found = level->entries.end();
            }
        }
        if (found == level->entries.end()) {
            auto entry = std::make_unique<Entry>();
            entry->kind = kind;
            auto status = make(*entry);
            if (!status.ok()) { fail(std::move(status)); return nullptr; }
            entry->children.element = entry->element;
            found = level->entries.emplace(key, std::move(entry)).first;
        }
        Entry& entry = *found->second;
        entry.seen = run;
        const std::size_t index = level->next++;
        const auto siblings = context.children(level->element);
        if (index >= siblings.size() || siblings[index] != entry.element) {
            auto status = context.reparent(entry.element, level->element, index);
            if (!status.ok()) { fail(std::move(status)); return nullptr; }
        }
        last = entry.element;
        return &entry;
    }

    base::Status make_widget(Entry& entry, detail::WidgetDesc widget_desc) {
        auto widget = detail::Widget::create(context, level->element, std::move(widget_desc));
        if (!widget.ok()) { return widget.status(); }
        entry.widget = std::move(widget).value();
        entry.element = entry.widget.element();
        return base::Status::success();
    }

    void update_text(Entry& entry, std::string_view text_value) {
        if (entry.text == text_value) { return; }
        auto status = entry.widget.set_text(std::string(text_value));
        if (!status.ok()) { fail(std::move(status)); return; }
        entry.text = std::string(text_value);
    }

    // Destroys items of `target` not described in this run.
    void finish(Level& target) {
        for (auto it = target.entries.begin(); it != target.entries.end();) {
            if (it->second->seen == run) { ++it; continue; }
            if (alive(it->second->element)) {
                auto status = context.destroy(it->second->element);
                if (!status.ok()) { fail(std::move(status)); }
            }
            it = target.entries.erase(it);
        }
    }

    void container(std::string_view id, Kind kind, float spacing,
                   const std::function<void()>& body_fn) {
        Entry* entry = enter(id, kind, [](const Entry&) { return true; }, [&](Entry& made) {
            auto element = context.create(level->element);
            if (!element.ok()) { return element.status(); }
            made.element = element.value();
            made.step = spacing;
            return context.set_layout(made.element, stack(kind, spacing));
        });
        if (entry == nullptr) { return; }
        if (entry->step != spacing) {
            auto status = context.set_layout(entry->element, stack(kind, spacing));
            if (!status.ok()) { fail(std::move(status)); return; }
            entry->step = spacing;
        }
        Level* outer = level;
        level = &entry->children;
        level->next = 0;
        if (body_fn) { body_fn(); }
        finish(*level);
        level = outer;
        last = entry->element;
    }

    base::Status execute() {
        ++run;
        error = base::Status::success();
        level = &root;
        root.next = 0;
        last = {};
        RegionScope scope(*this);
        if (body) { body(scope); }
        finish(root);
        return error;
    }
};

base::Result<Region> Region::create(Context& context, ElementId parent, Body body,
                                    RegionOptions options) {
    auto element = context.create(parent);
    if (!element.ok()) { return element.status(); }
    const ElementId id = element.value();
    auto status = context.set_layout(id, stack(Kind::column, options.spacing));
    if (status.ok()) {
        auto state = std::make_shared<RegionScope::State>(context, id, std::move(body),
                                                          options.text);
        status = context.set_updater(id, [state] { return state->execute(); }, options.live);
    }
    if (!status.ok()) {
        const auto cleanup = context.destroy(id);
        return cleanup.ok() ? status : cleanup;
    }
    return Region(context, id);
}

base::Status Region::invalidate() {
    if (context_ == nullptr) { return scope_error("GUI region is empty"); }
    return context_->request_update(element_);
}

base::Status Region::set_live(bool live) {
    if (context_ == nullptr) { return scope_error("GUI region is empty"); }
    return context_->set_update_live(element_, live);
}

void RegionScope::label(std::string_view id, std::string_view text) {
    Entry* entry = state_.enter(id, Kind::label, [](const Entry&) { return true; },
        [&](Entry& made) {
            made.text = std::string(text);
            return state_.make_widget(made, state_.desc(detail::WidgetKind::label, text));
        });
    if (entry != nullptr) { state_.update_text(*entry, text); }
}

bool RegionScope::button(std::string_view id, std::string_view text) {
    Entry* entry = state_.enter(id, Kind::button, [](const Entry&) { return true; },
        [&](Entry& made) {
            made.text = std::string(text);
            auto widget = state_.desc(detail::WidgetKind::button, text);
            widget.activated = [pending = made.pending, context = &state_.context,
                                region = state_.region] {
                ++pending->clicks;
                (void)context->request_update(region);
            };
            return state_.make_widget(made, std::move(widget));
        });
    if (entry == nullptr) { return false; }
    state_.update_text(*entry, text);
    const bool clicked = entry->pending->clicks > 0;
    entry->pending->clicks = 0;
    return clicked;
}

bool RegionScope::checkbox(std::string_view id, std::string_view text, bool& checked) {
    Entry* entry = state_.enter(id, Kind::checkbox, [](const Entry&) { return true; },
        [&](Entry& made) {
            made.text = std::string(text);
            auto widget = state_.desc(detail::WidgetKind::checkbox, text);
            widget.checked = checked;
            widget.toggled = [pending = made.pending, context = &state_.context,
                              region = state_.region](bool value) {
                pending->checked = value;
                (void)context->request_update(region);
            };
            return state_.make_widget(made, std::move(widget));
        });
    if (entry == nullptr) { return false; }
    state_.update_text(*entry, text);
    if (auto user = std::exchange(entry->pending->checked, std::nullopt)) {
        checked = *user;
        return true;
    }
    const auto shown = entry->widget.checked();
    if (shown.ok() && shown.value() != checked) {
        auto status = entry->widget.set_checked(checked);
        if (!status.ok()) { state_.fail(std::move(status)); }
    }
    return false;
}

bool RegionScope::slider(std::string_view id, float& value, float min, float max, float step) {
    Entry* entry = state_.enter(id, Kind::slider,
        [&](const Entry& old) { return old.min == min && old.max == max && old.step == step; },
        [&](Entry& made) {
            made.min = min;
            made.max = max;
            made.step = step;
            auto widget = state_.desc(detail::WidgetKind::slider, {});
            widget.min = min;
            widget.max = max;
            widget.value = value;
            widget.step = step;
            widget.value_changed = [pending = made.pending, context = &state_.context,
                                    region = state_.region](float changed) {
                pending->value = changed;
                (void)context->request_update(region);
            };
            return state_.make_widget(made, std::move(widget));
        });
    if (entry == nullptr) { return false; }
    if (auto user = std::exchange(entry->pending->value, std::nullopt)) {
        value = *user;
        return true;
    }
    const auto shown = entry->widget.value();
    if (shown.ok() && shown.value() != value) {
        auto status = entry->widget.set_value(value);
        if (!status.ok()) { state_.fail(std::move(status)); }
    }
    return false;
}

bool RegionScope::text_edit(std::string_view id, std::string& value, bool multiline) {
    Entry* entry = state_.enter(id, Kind::text_edit,
        [&](const Entry& old) { return old.step == (multiline ? 1.0F : 0.0F); },
        [&](Entry& made) {
            made.step = multiline ? 1.0F : 0.0F;
            auto widget = state_.desc(detail::WidgetKind::text_edit, value);
            widget.multiline = multiline;
            widget.text_changed = [pending = made.pending, context = &state_.context,
                                   region = state_.region](const std::string& changed) {
                pending->text = changed;
                (void)context->request_update(region);
            };
            return state_.make_widget(made, std::move(widget));
        });
    if (entry == nullptr) { return false; }
    if (auto user = std::exchange(entry->pending->text, std::nullopt)) {
        value = std::move(*user);
        return true;
    }
    const auto shown = entry->widget.text();
    if (!shown.ok()) { state_.fail(shown.status()); return false; }
    if (shown.value() != value) {
        auto status = entry->widget.set_text(value);
        if (!status.ok()) { state_.fail(std::move(status)); }
    }
    return false;
}

void RegionScope::row(std::string_view id, float spacing, const std::function<void()>& body) {
    state_.container(id, Kind::row, spacing, body);
}

void RegionScope::column(std::string_view id, float spacing, const std::function<void()>& body) {
    state_.container(id, Kind::column, spacing, body);
}

ElementId RegionScope::last() const noexcept { return state_.last; }
Context& RegionScope::context() const noexcept { return state_.context; }

} // namespace truffle::gui

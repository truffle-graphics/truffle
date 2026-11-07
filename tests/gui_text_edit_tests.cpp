#include "truffle/gui/specialist/text.hpp"
#include "gui_text_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/builder.hpp"
#include "truffle/gui/region.hpp"
#include "truffle/gui/text_edit.hpp"
#include "truffle/gui/themes.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace {

using namespace truffle;

#if TFL_TEST_GUI_TEXT
struct TestHost final : gui::Host {
    std::string clipboard;
    std::optional<gui::TextInputState> state;
    std::optional<gui::Rect> area;
    gui::Cursor cursor = gui::Cursor::arrow;
    std::string clipboard_text() override { return clipboard; }
    void set_clipboard_text(std::string_view text) override { clipboard = text; }
    void set_cursor(gui::Cursor value) override { cursor = value; }
    void set_text_input_area(std::optional<gui::Rect> value) override { area = value; }
    void set_text_input_state(std::optional<gui::TextInputState> value) override {
        state = std::move(value);
    }
};

struct Fixture {
    decltype(tests::make_null_context()) native = tests::make_null_context();
    gui::TextEngine engine = tests::require(gui::specialist::TextInterop::create(native.device));
    gui::FontId font = tests::require(engine.load_font(tests::gui_test_font(), 7));
    TestHost host;
    gui::Context context;
    int changes = 0;
    int submits = 0;
    gui::TextEdit edit;

    explicit Fixture(bool multiline = false) {
        context.set_host(&host);
        tests::require(context.set_viewport({200, 120}));
        edit = tests::require(gui::TextEdit::create(context, context.root(), {
            .text = "ABA", .text_engine = &engine, .font = font, .atlas = gui::TextureId{1},
            .multiline = multiline, .changed = [this](const std::string&) { ++changes; },
            .submitted = [this] { ++submits; }}));
        frame();
        tests::require(context.focus(edit.element()));
        frame();
    }
    void frame() { tests::require(context.prepare_frame(gui::Context::Clock::now()).status); }
    void key(gui::Key key, gui::Modifiers modifiers = gui::Modifiers::none) {
        tests::require(context.handle(gui::KeyEvent{.key = key, .modifiers = modifiers}));
    }
    std::string text() const { return tests::require(edit.text()); }
};

int editing_clipboard_history() {
    Fixture f;
    TFL_CHECK(f.host.state && f.host.area);
    TFL_CHECK(f.edit.select({1, 2}).ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"BB"}).ok());
    TFL_CHECK(f.text() == "ABBA" && f.changes == 1);
    TFL_CHECK((f.edit.selection().value() == gui::TextSelection{3, 3}));
    f.key(gui::Key::backspace);
    TFL_CHECK(f.text() == "ABA" && f.changes == 2);
    f.key(gui::Key::delete_forward);
    TFL_CHECK(f.text() == "AB" && f.changes == 3);
    f.key(gui::Key::z, gui::Modifiers::super);
    TFL_CHECK(f.text() == "ABA" && f.changes == 4);
    f.key(gui::Key::z, gui::Modifiers::super | gui::Modifiers::shift);
    TFL_CHECK(f.text() == "AB" && f.changes == 5);
    f.key(gui::Key::a, gui::Modifiers::control);
    f.key(gui::Key::c, gui::Modifiers::control);
    TFL_CHECK(f.host.clipboard == "AB");
    f.key(gui::Key::x, gui::Modifiers::super);
    TFL_CHECK(f.text().empty());
    f.key(gui::Key::v, gui::Modifiers::super);
    TFL_CHECK(f.text() == "AB");
    f.key(gui::Key::enter);
    TFL_CHECK(f.submits == 1);
    const auto changes = f.changes;
    TFL_CHECK(f.edit.set_text("A").ok());
    TFL_CHECK(f.changes == changes);
    TFL_CHECK(f.edit.undo().ok() && f.text() == "A");
    f.frame();
    TFL_CHECK(!f.context.needs_frame(gui::Context::Clock::now()));
    TFL_CHECK(f.edit.set_text("A").ok());
    TFL_CHECK(!f.context.needs_frame(gui::Context::Clock::now()));
    f.key(gui::Key::end);
    TFL_CHECK(!f.context.needs_frame(gui::Context::Clock::now()));
    const auto idle = f.context.prepare_frame(gui::Context::Clock::now());
    TFL_CHECK(idle.layout.measured == 0 && idle.layout.arranged == 0 &&
              idle.shapes.regenerated == 0);
    return 0;
}

int unicode_selection_and_failures() {
    Fixture f;
    const std::string combined = "A\xCC\x81" "B";
    TFL_CHECK(f.edit.set_text(combined).ok());
    TFL_CHECK(!f.edit.select({1, 1}).ok());
    TFL_CHECK(f.edit.select({3, 3}).ok());
    f.key(gui::Key::backspace);
    TFL_CHECK(f.text() == "B");
    TFL_CHECK(f.edit.undo().ok() && f.text() == combined);
    TFL_CHECK(f.edit.select({0, 0}).ok());
    f.key(gui::Key::delete_forward);
    TFL_CHECK(f.text() == "B");
    const std::string emoji = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
    TFL_CHECK(f.edit.set_text("A" + emoji + "B").ok());
    TFL_CHECK(f.edit.select({1 + emoji.size(), 1 + emoji.size()}).ok());
    f.key(gui::Key::backspace);
    TFL_CHECK(f.text() == "AB");
    const auto before = f.text();
    TFL_CHECK(!f.context.handle(gui::TextEvent{std::string("\xC0\x80", 2)}).ok());
    TFL_CHECK(f.text() == before);
    TFL_CHECK(!f.context.handle(gui::TextEvent{"A", gui::TextRange{4, 8}}).ok());
    TFL_CHECK(!f.edit.set_font(gui::FontId{123}).ok());
    TFL_CHECK(f.text() == before);
    TFL_CHECK(f.edit.set_text("A").ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"\xCC\x81"}).ok());
    TFL_CHECK(f.text() == std::string("A\xCC\x81"));
    TFL_CHECK(f.edit.selection().value().caret == 3);
    return 0;
}

int input_method_lifecycle() {
    Fixture f;
    TFL_CHECK(f.edit.select({1, 2}).ok());
    TFL_CHECK(f.context.handle(gui::CompositionEvent{
        .preedit = "AA", .selection_begin = 1, .selection_end = 2}).ok());
    TFL_CHECK(f.text() == "ABA" && f.changes == 0);
    TFL_CHECK(f.host.state->text == "AAAA");
    TFL_CHECK((f.host.state->marked == gui::TextRange{1, 3}));
    TFL_CHECK((f.host.state->selection == gui::TextRange{2, 3}));
    TFL_CHECK(f.context.handle(gui::CompositionEvent{
        .preedit = "B", .selection_begin = 1, .selection_end = 1}).ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"BB"}).ok());
    TFL_CHECK(f.text() == "ABBA" && f.changes == 1);
    TFL_CHECK(!f.host.state->marked);
    TFL_CHECK(f.edit.undo().ok() && f.text() == "ABA");
    TFL_CHECK(f.context.handle(gui::CompositionEvent{
        .preedit = "A", .replacement = gui::TextRange{0, 1}}).ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"B", gui::TextRange{0, 1}}).ok());
    TFL_CHECK(f.text() == "BBA");
    TFL_CHECK(f.context.handle(gui::CompositionEvent{.preedit = "A"}).ok());
    const auto before = f.text();
    const auto count = f.changes;
    TFL_CHECK(f.context.handle(gui::FocusEvent{false}).ok());
    TFL_CHECK(f.text() == before && f.changes == count);
    TFL_CHECK(!f.host.area && !f.host.state);
    TFL_CHECK(f.context.handle(gui::FocusEvent{true}).ok());
    TFL_CHECK(f.context.focus(f.edit.element()).ok());
    TFL_CHECK(f.host.state && !f.host.state->marked);
    TFL_CHECK(f.context.handle(gui::CompositionEvent{.preedit = "B"}).ok());
    TFL_CHECK(f.context.destroy(f.edit.element()).ok());
    TFL_CHECK(!f.host.state && !f.host.area && !f.edit.alive());
    TFL_CHECK(f.context.input().composition.preedit.empty());
    TFL_CHECK(!f.edit.set_text("A").ok());
    return 0;
}

int bidirectional_visual_navigation() {
    Fixture f;
    const std::string mixed = "A \u05D0\u05D1 B";
    TFL_CHECK(f.edit.set_text(mixed).ok());
    const auto layout = tests::require(f.engine.shape(f.font, mixed));
    TFL_CHECK(f.edit.select({0, 0}).ok());
    for (std::size_t i = 1; i < layout.carets.size(); ++i) {
        f.key(gui::Key::right);
        TFL_CHECK(f.edit.selection().value().caret == layout.carets[i].byte);
    }
    for (std::size_t i = layout.carets.size() - 1; i > 0; --i) {
        f.key(gui::Key::left);
        TFL_CHECK(f.edit.selection().value().caret == layout.carets[i - 1].byte);
    }
    TFL_CHECK(f.edit.select({0, 1}).ok());
    f.key(gui::Key::left);
    TFL_CHECK((f.edit.selection().value() == gui::TextSelection{0, 0}));
    TFL_CHECK(f.edit.select({0, 1}).ok());
    f.key(gui::Key::right);
    TFL_CHECK((f.edit.selection().value() == gui::TextSelection{1, 1}));
    return 0;
}

int pointer_read_only_multiline() {
    Fixture f(true);
    TFL_CHECK(f.edit.set_text("AB\nBA").ok());
    TFL_CHECK(f.edit.select({5, 5}).ok());
    f.key(gui::Key::home);
    TFL_CHECK(f.edit.selection().value().caret == 3);
    f.key(gui::Key::end, gui::Modifiers::shift);
    TFL_CHECK((f.edit.selection().value() == gui::TextSelection{3, 5}));
    f.key(gui::Key::enter);
    TFL_CHECK(f.text() == "AB\n\n" && f.submits == 0);
    TFL_CHECK(f.edit.set_read_only(true).ok());
    const auto before = f.text();
    f.key(gui::Key::a, gui::Modifiers::control);
    f.key(gui::Key::x, gui::Modifiers::control);
    TFL_CHECK(f.text() == before && f.host.clipboard == before);
    TFL_CHECK(f.context.handle(gui::TextEvent{"A"}).ok() && f.text() == before);
    TFL_CHECK(f.edit.set_read_only(false).ok());
    const auto caret = tests::require(f.edit.caret_rect());
    TFL_CHECK(f.context.set_child_offset(f.context.root(), {10, 15}).ok());
    const auto shifted = tests::require(f.edit.caret_rect());
    TFL_CHECK(shifted.x == caret.x + 10 && shifted.y == caret.y + 15);
    TFL_CHECK(f.context.set_child_offset(f.context.root(), {}).ok());
    TFL_CHECK(f.edit.set_text("ABABABABABABABABABABABABABABABAB").ok());
    f.frame();
    const auto bounds = tests::require(f.context.absolute_bounds(f.edit.element()));
    const gui::Point point{bounds.x + 10, bounds.y + 8};
    TFL_CHECK(f.context.handle(gui::PointerEvent{
        .action = gui::PointerAction::down, .position = point,
        .button = gui::PointerButton::primary}).ok());
    TFL_CHECK(f.context.captured() == f.edit.element());
    TFL_CHECK(f.context.handle(gui::PointerEvent{
        .action = gui::PointerAction::move, .position = {point.x + 20, point.y}}).ok());
    TFL_CHECK(f.edit.selection().value().caret != f.edit.selection().value().anchor);
    TFL_CHECK(f.context.handle(gui::PointerEvent{
        .action = gui::PointerAction::up, .position = point,
        .button = gui::PointerButton::primary}).ok());
    TFL_CHECK(!f.context.captured().valid());
    return 0;
}

int authoring_and_callback_lifetime() {
    Fixture f;
    gui::Widget built;
    const auto tree = gui::ui::text_edit("AB").bind(built);
    TFL_CHECK(gui::build(f.context, f.context.root(), tree,
        {&f.engine, f.font, gui::TextureId{1}}).ok());
    TFL_CHECK(built.editor().ok() && built.text().value() == "AB");
    std::string model = "A";
    bool changed = false;
    auto region = tests::require(gui::Region::create(f.context, f.context.root(),
        [&](gui::RegionScope& scope) { changed = scope.text_edit("edit", model); },
        {.text = {&f.engine, f.font, gui::TextureId{1}}}));
    f.frame();
    const auto child = f.context.children(region.element()).front();
    TFL_CHECK(f.context.focus(child).ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"B"}).ok());
    f.frame();
    TFL_CHECK(changed && model == "BA");
    TFL_CHECK(f.context.request_update(region.element()).ok());
    f.frame();
    TFL_CHECK(!changed);
    model = "AB";
    TFL_CHECK(region.invalidate().ok());
    f.frame();
    TFL_CHECK(!changed && f.context.alive(child));
    gui::TextEdit doomed;
    doomed = tests::require(gui::TextEdit::create(f.context, f.context.root(), {
        .text_engine = &f.engine, .font = f.font, .atlas = gui::TextureId{1},
        .changed = [&](const std::string&) { tests::require(f.context.destroy(doomed.element())); }}));
    TFL_CHECK(f.context.focus(doomed.element()).ok());
    TFL_CHECK(f.context.handle(gui::TextEvent{"A"}).ok());
    TFL_CHECK(!doomed.alive() && !f.host.state);
    return 0;
}
#endif

int theme_tokens() {
    Fixture f;
    const auto grey = [](float v) { return gui::Color{v, v, v, 1}; };
    const auto decoration = [&] { return tests::require(f.context.decoration(f.edit.element())); };
    TFL_CHECK(decoration().fill == grey(0.12F) && decoration().border == grey(0.82F));
    TFL_CHECK(gui::apply_theme(f.context, gui::BuiltinTheme::truffle_light).ok());
    f.frame();
    TFL_CHECK(decoration().fill == grey(0.96F) && decoration().border == grey(0.22F));
    TFL_CHECK(f.context.handle(gui::FocusEvent{false}).ok());
    f.frame();
    TFL_CHECK(decoration().border == grey(0.72F));
    return 0;
}

} // namespace

int main() {
#if TFL_TEST_GUI_TEXT
    for (auto test : {editing_clipboard_history, unicode_selection_and_failures,
                      input_method_lifecycle, bidirectional_visual_navigation,
                      pointer_read_only_multiline,
                      authoring_and_callback_lifetime, theme_tokens}) {
        if (const int result = test(); result != 0) { return result; }
    }
#else
    auto native = tests::make_null_context();
    gui::TextEngine empty;
    gui::Context context;
    auto result = gui::TextEdit::create(context, context.root(), {
        .text_engine = &empty, .font = gui::FontId{1}, .atlas = gui::TextureId{1}});
    TFL_CHECK(!result.ok() && result.status().code == base::StatusCode::unsupported);
#endif
    return 0;
}

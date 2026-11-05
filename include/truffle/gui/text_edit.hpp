#pragma once

#include "truffle/gui/context.hpp"

#include <functional>
#include <memory>
#include <string>

namespace truffle::gui {

struct TextSelection {
    std::size_t anchor = 0;
    std::size_t caret = 0;
    friend bool operator==(const TextSelection&, const TextSelection&) = default;
};

struct TextEditDesc {
    std::string text;
    TextEngine* text_engine = nullptr;
    FontId font;
    TextureId atlas;
    bool multiline = false;
    bool read_only = false;
    // User edits only; programmatic setters never invoke these callbacks.
    std::function<void(const std::string&)> changed;
    std::function<void()> submitted;
};

// Context owns the control and its editing state. This checked reference and
// its borrowed text engine/atlas must not outlive the Context and renderer.
// Positions are UTF-8 bytes at extended-grapheme boundaries, not glyph indices.
class TextEdit {
public:
    TextEdit() = default;
    [[nodiscard]] static base::Result<TextEdit> create(
        Context& context, ElementId parent, TextEditDesc desc);
    [[nodiscard]] ElementId element() const noexcept;
    [[nodiscard]] bool alive() const noexcept;
    [[nodiscard]] base::Result<std::string> text() const;
    [[nodiscard]] base::Status set_text(std::string text);
    [[nodiscard]] base::Result<TextSelection> selection() const;
    // Current caret in root coordinates, including ancestor scroll offsets.
    [[nodiscard]] base::Result<Rect> caret_rect() const;
    [[nodiscard]] base::Status select(TextSelection selection);
    [[nodiscard]] base::Status set_read_only(bool read_only);
    [[nodiscard]] base::Status set_font(FontId font);
    [[nodiscard]] base::Status undo();
    [[nodiscard]] base::Status redo();

    struct State;

private:
    explicit TextEdit(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace truffle::gui

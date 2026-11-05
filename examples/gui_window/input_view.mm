#include "input_view.hpp"

#include <algorithm>

namespace {

using namespace truffle::gui;

NSString* string_of(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string utf8_of(NSString* text) {
    return text == nil ? std::string{} : std::string(
        text.UTF8String, [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}

std::size_t byte_at(NSString* text, NSUInteger index) {
    return [[text substringToIndex:std::min(index, text.length)]
        lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
}

NSRange native_range(const std::string& text, truffle::gui::TextRange range) {
    const auto start = string_of(std::string_view(text).substr(0, range.begin)).length;
    const auto end = string_of(std::string_view(text).substr(0, range.end)).length;
    return NSMakeRange(start, end - start);
}

std::optional<truffle::gui::TextRange> byte_range(
    const std::optional<TextInputState>& state, NSRange range) {
    if (!state || range.location == NSNotFound) { return std::nullopt; }
    NSString* text = string_of(state->text);
    const auto begin = std::min(range.location, text.length);
    const auto length = std::min(range.length, text.length - begin);
    return truffle::gui::TextRange{byte_at(text, begin), byte_at(text, begin + length)};
}

NSString* plain(id value) {
    return [value isKindOfClass:NSAttributedString.class]
        ? [(NSAttributedString*)value string] : (NSString*)value;
}

} // namespace

@implementation TFLInputView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)keyDown:(NSEvent*)event {
    if (textState) { [self interpretKeyEvents:@[event]]; }
}
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // Physical shortcut events have already gone to Context::handle.
    if (!textState || (event.modifierFlags & NSEventModifierFlagCommand) == 0) { return NO; }
    switch (event.keyCode) {
    case 0: case 6: case 7: case 8: case 9: case 16: return YES;
    default: return NO;
    }
}
- (void)insertText:(id)value replacementRange:(NSRange)range {
    if (!forward || !textState) { return; }
    NSString* text = plain(value);
    forward(TextEvent{utf8_of(text), byte_range(textState, range)});
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected
      replacementRange:(NSRange)range {
    if (!forward || !textState) { return; }
    NSString* text = plain(value);
    const auto begin = std::min(selected.location, text.length);
    const auto count = std::min(selected.length, text.length - begin);
    forward(CompositionEvent{.preedit = utf8_of(text),
        .selection_begin = byte_at(text, begin), .selection_end = byte_at(text, begin + count),
        .replacement = byte_range(textState, range)});
}
- (void)unmarkText {
    if (forward && textState && textState->marked) {
        // AppKit unmark commits the currently marked string.
        const auto range = *textState->marked;
        const auto text = textState->text.substr(range.begin, range.end - range.begin);
        forward(TextEvent{text});
    }
}
- (BOOL)hasMarkedText { return textState && textState->marked.has_value(); }
- (NSRange)markedRange {
    return textState && textState->marked
        ? native_range(textState->text, *textState->marked) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange {
    return textState ? native_range(textState->text, textState->selection)
                     : NSMakeRange(NSNotFound, 0);
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                              actualRange:(NSRangePointer)actual {
    if (!textState || range.location == NSNotFound) { return nil; }
    NSString* text = string_of(textState->text);
    const auto begin = std::min(range.location, text.length);
    const auto count = std::min(range.length, text.length - begin);
    const auto bounded = NSMakeRange(begin, count);
    if (actual != nullptr) { *actual = bounded; }
    return [[NSAttributedString alloc] initWithString:[text substringWithRange:bounded]];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return [self selectedRange].location;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual != nullptr) { *actual = range; }
    if (queryArea) { candidateArea = queryArea(); }
    if (!candidateArea) { return NSZeroRect; }
    const auto r = *candidateArea;
    const NSRect local = NSMakeRect(r.x, r.y, r.width, r.height);
    return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
}
- (void)doCommandBySelector:(SEL)selector {
    // Without preedit, Window::translate already forwarded physical keys.
    // With preedit, AppKit consumes editing keys first and sends commands here
    // only when the input method did not consume them.
    if (![self hasMarkedText] || !forward) { return; }
    Key key = Key::unknown;
    if (selector == @selector(deleteBackward:)) { key = Key::backspace; }
    else if (selector == @selector(deleteForward:)) { key = Key::delete_forward; }
    else if (selector == @selector(moveLeft:) ||
             selector == @selector(moveLeftAndModifySelection:)) { key = Key::left; }
    else if (selector == @selector(moveRight:) ||
             selector == @selector(moveRightAndModifySelection:)) { key = Key::right; }
    else if (selector == @selector(moveUp:)) { key = Key::up; }
    else if (selector == @selector(moveDown:)) { key = Key::down; }
    else if (selector == @selector(insertNewline:)) { key = Key::enter; }
    else if (selector == @selector(insertTab:)) { key = Key::tab; }
    else if (selector == @selector(cancelOperation:)) { key = Key::escape; }
    if (key != Key::unknown) {
        const bool shift = (NSApp.currentEvent.modifierFlags & NSEventModifierFlagShift) != 0;
        forward(KeyEvent{.key = key,
            .modifiers = shift ? Modifiers::shift : Modifiers::none});
    }
}

@end

#pragma once

#import <AppKit/AppKit.h>

#include "truffle/gui/host.hpp"

#include <functional>

// Host-owned input-method adapter. The GUI only sees UTF-8 events and ranges.
@interface TFLInputView : NSView <NSTextInputClient> {
@public
    std::function<void(const truffle::gui::InputEvent&)> forward;
    std::optional<truffle::gui::TextInputState> textState;
    std::optional<truffle::gui::Rect> candidateArea;
    std::function<std::optional<truffle::gui::Rect>()> queryArea;
}
@end

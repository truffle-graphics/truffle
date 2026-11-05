# GUI Window

A macOS application demonstrating the core widgets, editable text and the retained
GUI's selective work. The host
owns the Cocoa window, `CAMetalLayer`, event loop, swapchain and presentation;
`Truffle::GUI` owns the persistent element tree, layout, shapes and frame
scheduling.

The loop sleeps until input or the GUI's next timed update, translates AppKit
events into GUI input events, passes the logical size and content scale to
`Context::set_viewport`, and draws only when `Context::needs_frame` is true.
The left panel has a label, button, checkbox, slider, image and text editor. The right scroll
area contains 64 persistent rows. The button updates its count, the checkbox
changes the accent theme, and the slider changes image tint. Tab/Shift+Tab move
focus, Space/Enter activate controls, arrows change the slider, and wheel or
arrow/Page keys scroll the focused scroll area. Layout, drawing, hit testing and
scroll clipping use the same tree. `demo.hpp` owns application composition;
`main.mm` owns the host, font lookup, rendering and presentation;
`input_view.mm` implements `NSTextInputClient`, UTF-8/UTF-16 range conversion,
composition and candidate-window placement. The editor supports selection,
clipboard shortcuts and undo/redo; it displays mixed Hebrew/Arabic/Latin text
through host-discovered fallback fonts. Its font chain is checked for missing
glyphs at startup.

No font is redistributed: the host loads its installed system font into
`TextEngine`, with CoreText-selected Hebrew/Arabic fallback font files.
GUI text must be enabled (the default). A text-disabled build does
not build this text-dependent example.

## Build and run

Requires macOS with a logged-in GUI session and a Metal device. The GUI program ships as prepared MSL, so no shader compiler is needed.

```sh
cmake -S . -B build/gui-window \
  -DTFL_BUILD_BACKEND_METAL=ON \
  -DTFL_BUILD_EXAMPLES=ON
cmake --build build/gui-window --target truffle_gui_window
./build/gui-window/examples/gui_window/truffle_gui_window
```

The close button exits and prints the number of frames drawn. Escape closes
when no editor is focused; inside the editor it cancels input-method preedit.

`--smoke` scripts the same native window/tree, checks exact work counts, and
fails on unexpected work. Unrelated physical pointer/key events are ignored in
the script; normal mode forwards them. Host focus and resize still pass through.

```sh
ctest --test-dir build/gui-window --output-on-failure -R '^truffle_gui_window$'
```

## Persistence evidence (#229)

Local Apple Silicon / native Metal with API and shader validation, Debug and
warnings as errors. The tree has **148 elements**, including all 64 rows and
the editor added in #252.
Counters count recomputation, not cache hits or GPU redrawing.

| Change | Style resolutions | Measurements | Arrangements | Shape regenerations |
| --- | ---: | ---: | ---: | ---: |
| Initial tree | 143 | 148 | 148 | 148 |
| Idle (even if prepared explicitly) | 0 | 0 | 0 | 0 |
| Accent theme (two paint dependents) | 2 | 0 | 0 | 2 |
| Scroll by one row | 0 | 0 | 0 | 0 |
| One label's text | 0 | 4 | 4 | 1 |
| Button gesture and counter callback | 1 | 4 | 4 | 2 |
| Checkbox gesture and accent callback | 4 | 0 | 0 | 4 |
| Slider key and image-tint callback | 2 | 0 | 0 | 3 |
| Window resize, 900x640 to 740x560 | 0 | 4 | 7 | 7 |

The original interaction script draws eight frames and remains idle.
The subsequent native editing script calls `NSTextInputClient::setMarkedText`
and `insertText`, checks that preedit leaves committed text unchanged, validates
a nonempty candidate rectangle, commits one undoable edit and returns to idle.
It draws one additional frame (nine total).
Idle, theme, scroll and text counts are asserted, as are widget callback
results and frame counts. Row virtualization is deliberately not implemented
here (#234): the elements and their shapes persist, while frame assembly
still visits the tree. Zero recomputation is not a zero-cost traversal claim.
Independent `truffle_gui_drawing_metal_tests` compare native widget pixels,
including glyphs, hover/press/release, slider/check state and clipped scrolling.

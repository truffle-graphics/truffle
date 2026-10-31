# GUI Window

A small macOS application showing the Truffle GUI **host boundary**. The host
owns the Cocoa window, `CAMetalLayer`, event loop, swapchain and presentation;
`Truffle::GUI` owns the persistent element tree, layout, shapes and frame
scheduling.

The loop sleeps until input or the GUI's next timed update, translates AppKit
events into GUI input events, passes the logical size and content scale to
`Context::set_viewport`, and draws only when `Context::needs_frame` is true.
Moving the pointer alone draws nothing; clicking or dragging in the canvas moves
the marker, the sidebar indicator blinks on a timer, and resizing redraws once.
Cmd+C copies the marker position through the `Host` clipboard service. Event
routing to elements, text and widgets arrive in later GUI issues.

## Build and run

Requires macOS with a logged-in GUI session and a Metal device. The GUI program
ships as prepared MSL, so no shader compiler is needed.

```sh
cmake -S . -B build/gui-window \
  -DTRUFFLE_BUILD_BACKEND_METAL=ON \
  -DTRUFFLE_BUILD_EXAMPLES=ON
cmake --build build/gui-window --target truffle_gui_window
./build/gui-window/examples/gui_window/truffle_gui_window
```

Escape or the close button exits and prints the number of frames drawn.

`--smoke` scripts the same window and fails unless it draws exactly one frame
initially, none while idle, one for a timed update, none for pointer movement,
and one each for a marker placement and a window resize:

```sh
ctest --test-dir build/gui-window --output-on-failure -R '^truffle_gui_window$'
```

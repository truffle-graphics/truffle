# Native dock windows

On a logged-in Apple Silicon macOS desktop with the Metal backend enabled:

```sh
cmake --build build/gui-interaction --target truffle_gui_dock_window
build/gui-interaction/examples/gui_dock_window/truffle_gui_dock_window
```

Drag the left tab outside the main window to detach the live colored button.
Click it to change its color; closing its native window redocks the same
element and handler. The other panel and application state remain intact.
The host owns Cocoa windows, desktop-coordinate conversion, input, Metal
surfaces, swapchains and pacing. No native windowing implementation is part
of GUI Dock or GUI.

`--smoke` runs a bounded script with real Cocoa events and two visible native
windows/Metal swapchains. It checks detached rendering, resize, interaction,
native close/redock, restored state, and resource cleanup. CTest registers it
with Metal API/shader validation and a 40-second deadline. This is macOS
evidence only; the example does not implement native text editing/IME.

## Portable lifecycle

`DockWindowHost::open_window` prepares a hidden window and returns an empty
`gui::Surface` from the **same Context**. Refusal/failure leaves the dock model
and content intact. After validating the result, Dock reparents the original
live subtree and commits the layout, then calls `show_window`. Closing must
call `DockSpace::redock` first and keep the window open if that fails.

A Surface is an independent logical root of one Context, not a copy or a
second Context. Its input, focus/capture, host and scale are view-local.
Styles, updates, invalidation and layout/shapes share one Context frame pass;
the host draws all affected native views after that pass. Each native window
needs separate Painter streaming buffers/pacing, as demonstrated here.
Context must outlive its Surfaces; the borrowed DockWindowHost must outlive
dock shutdown. A host must not mutate/reenter the dock while opening/closing
a window. Native callbacks defer object retirement until dispatch finishes.

`save` and named presets normalize detached panels into a portable dock
layout. `restore`/`set_layout` return live panels to the dock and close their
native windows; restoring does **not** silently create OS windows. A redock
restores its original split/tab location when the remaining layout is
unchanged. After unrelated layout edits it appends the panel to the first
remaining tab area (or creates an area if the dock is empty).

Call `shutdown` before releasing the host or Context. Destroying the dock
element also reclaims detached content before its host windows close.
Hosts must retire a Surface only after any in-flight call to that Surface
has returned; this example queues closed windows for retirement on the next
host iteration.

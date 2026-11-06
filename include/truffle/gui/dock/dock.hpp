#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/dock/layout.hpp"
#include "truffle/gui/surface.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace truffle::gui::dock {

// Dock geometry only; colours and radii come from the `dock.*` theme tokens
// (see truffle/gui/themes.hpp), so layout stays independent of the theme.
struct DockMetrics {
    float tab_height = 24.0F;
    float max_tab_width = 160.0F;
    float divider = 4.0F;        // draggable splitter thickness
    float gap = 0.0F;            // extra space between neighbouring areas; shows dock.background
    float min_area = 48.0F;      // smallest size a divider drag leaves an area
    float drag_threshold = 4.0F; // pointer travel before a tab press becomes a drag
};

struct DockSpaceOptions {
    DockMetrics metrics;
    // Tab titles are shaped when an engine is given; it must outlive the dock.
    TextEngine* text_engine = nullptr;
    FontId font;
    TextureId atlas;
};

// What dropping the dragged tab at the current pointer would do; `rect` is the
// marker in the dock element's coordinates.
struct DropPreview {
    std::string panel;
    std::string anchor;
    DropSide side = DropSide::center;
    Rect rect;
};

// The host prepares an initially hidden native window and a Surface of the
// SAME Context. It must not move content or mutate the dock while opening.
struct DockWindowRequest {
    std::string panel;
    std::string title;
    Rect rect; // requested window rectangle in the dock element's coordinates
};

class DockWindowHost {
public:
    DockWindowHost() = default;
    DockWindowHost(const DockWindowHost&) = delete;
    DockWindowHost& operator=(const DockWindowHost&) = delete;
    virtual ~DockWindowHost() = default;

    // Refusal/failure returns a Status and cleans up partial native resources.
    // Success transfers no GUI ownership: the returned empty Surface remains
    // host-owned and valid until close_window. GUI Dock moves the live content
    // only after validating it. Neither call may reenter the dock.
    [[nodiscard]] virtual base::Result<Surface*> open_window(
        Context& context, const DockWindowRequest& request) = 0;
    virtual void show_window(std::string_view panel) noexcept = 0;
    // Must release native resources and its Surface, without destroying panel
    // content (Dock has already moved it back). Must not reenter the dock.
    virtual void close_window(std::string_view panel) noexcept = 0;
};

// Docking inside one element: registered panels are shown in tabbed areas laid
// out by a DockNode. Dragging a tab shows a drop marker for the left, right,
// top, bottom or center (tab) of the area under the pointer; dropping moves the
// panel. Dividers between areas resize them. Panel contents are reparented,
// never recreated; inactive or undocked contents wait in a hidden, input-disabled
// holder. The handle is cheap to copy; the state lives with the dock element.
class DockSpace {
public:
    DockSpace() = default;

    [[nodiscard]] static base::Result<DockSpace> create(Context& context, ElementId parent,
                                                        DockSpaceOptions options = {});

    [[nodiscard]] ElementId element() const noexcept;
    [[nodiscard]] bool alive() const noexcept;

    // Registers a panel and takes `content` (reparented into the dock). The
    // panel is hidden until a layout or `dock` places it.
    [[nodiscard]] base::Status add_panel(std::string id, std::string title, ElementId content);
    // Undocks and unregisters a panel; its content is reparented to `new_parent`.
    [[nodiscard]] base::Status remove_panel(std::string_view id, ElementId new_parent);

    // Every panel in `layout` must be registered.
    [[nodiscard]] base::Status set_layout(DockNode layout);
    [[nodiscard]] const DockNode& layout() const noexcept;
    // Inserts or moves `panel` beside the area holding `anchor`.
    [[nodiscard]] base::Status dock(std::string_view panel, std::string_view anchor, DropSide side);
    [[nodiscard]] base::Status undock(std::string_view panel);
    [[nodiscard]] base::Status activate(std::string_view panel);

    // Saved layouts normalize detached panels back into the dock. Restore is
    // portable and closes native windows; it never silently opens OS windows.
    [[nodiscard]] std::string save() const;
    [[nodiscard]] base::Status restore(std::string_view text);
    [[nodiscard]] DockPresets& presets() noexcept;
    [[nodiscard]] base::Status save_preset(std::string name);
    [[nodiscard]] base::Status load_preset(std::string_view name);

    [[nodiscard]] std::optional<DropPreview> drop_preview() const;
    [[nodiscard]] ElementId tab(std::string_view panel) const noexcept;
    // Divider elements in tree order of their splits.
    [[nodiscard]] std::span<const ElementId> dividers() const noexcept;

    // Borrowed; must outlive the dock's shutdown, including element destruction.
    // Replacing a host with open windows is rejected.
    [[nodiscard]] base::Status set_window_host(DockWindowHost* host);
    [[nodiscard]] base::Status detach(std::string_view panel, Rect rect);
    // Native close requests call redock BEFORE closing their window. A failed
    // redock leaves the native window and live content intact.
    [[nodiscard]] base::Status redock(std::string_view panel);
    [[nodiscard]] bool detached(std::string_view panel) const noexcept;
    // Returns every detached panel to its hidden holder and closes all windows.
    // Call before destroying the Context or host; native cleanup is also tied
    // to destruction of the dock element, not to copies of this handle.
    [[nodiscard]] base::Status shutdown();

    struct State; // implementation detail

private:
    explicit DockSpace(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace truffle::gui::dock

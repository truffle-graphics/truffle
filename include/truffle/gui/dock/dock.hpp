#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/dock/layout.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace truffle::gui::dock {

struct DockStyle {
    float tab_height = 24.0F;
    float max_tab_width = 160.0F;
    float divider = 4.0F;
    float min_area = 48.0F;     // smallest size a divider drag leaves an area
    float drag_threshold = 4.0F; // pointer travel before a tab press becomes a drag
    Decoration area{.fill = {0.12F, 0.12F, 0.13F, 1.0F}};
    Decoration tab{.fill = {0.17F, 0.17F, 0.19F, 1.0F}};
    Decoration active_tab{.fill = {0.24F, 0.24F, 0.28F, 1.0F}};
    Decoration divider_fill{.fill = {0.07F, 0.07F, 0.08F, 1.0F}};
    Decoration marker{.fill = {0.25F, 0.5F, 1.0F, 0.3F},
                      .border = {0.25F, 0.5F, 1.0F, 0.9F},
                      .border_width = 2.0F};
};

struct DockSpaceOptions {
    DockStyle style;
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

// Separate operating-system windows are not implemented yet. A later version
// asks this host to create a window (with its own swapchain and Context) for a
// panel dropped outside the dock; the application owns that window.
struct DockWindowRequest {
    std::string panel;
    Rect rect; // requested window rectangle in the dock element's coordinates
};

class DockWindowHost {
public:
    DockWindowHost() = default;
    DockWindowHost(const DockWindowHost&) = delete;
    DockWindowHost& operator=(const DockWindowHost&) = delete;
    virtual ~DockWindowHost() = default;

    // Returns false to keep the panel docked.
    [[nodiscard]] virtual bool open_window(const DockWindowRequest& request) = 0;
    virtual void close_window(std::string_view panel) = 0;
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

    [[nodiscard]] std::string save() const;
    [[nodiscard]] base::Status restore(std::string_view text);
    [[nodiscard]] DockPresets& presets() noexcept;
    [[nodiscard]] base::Status save_preset(std::string name);
    [[nodiscard]] base::Status load_preset(std::string_view name);

    [[nodiscard]] std::optional<DropPreview> drop_preview() const;
    [[nodiscard]] ElementId tab(std::string_view panel) const noexcept;
    // Divider elements in tree order of their splits.
    [[nodiscard]] std::span<const ElementId> dividers() const noexcept;

    // Borrowed; reserved for separate windows (not called in this version).
    void set_window_host(DockWindowHost* host) noexcept;

    struct State; // implementation detail

private:
    explicit DockSpace(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace truffle::gui::dock

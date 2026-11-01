#pragma once

#include "truffle/base/status.hpp"
#include "truffle/gui/element.hpp"
#include "truffle/gui/events.hpp"
#include "truffle/gui/geometry.hpp"
#include "truffle/gui/host.hpp"
#include "truffle/gui/input.hpp"
#include "truffle/gui/layout.hpp"
#include "truffle/gui/shapes.hpp"
#include "truffle/gui/text.hpp"
#include "truffle/gui/theme.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace truffle::gui {

struct FrameStats {
    base::Status status;
    StyleStats styles;
    LayoutStats layout;
    ShapeStats shapes;
};

// Owns a persistent tree of elements. A context always has a root element.
// It is not thread-safe; use it from one thread at a time.
//
// Invalidation rules:
// - `measure` on an element also marks its own `arrange` and `shape`, and marks
//   `measure` and `arrange` on every ancestor, because their sizes and the
//   placement of siblings may depend on it.
// - `arrange` marks the element's own `arrange` and `shape`.
// - `style` and `shape` mark only the element itself.
// - Adding, removing or moving a child marks the affected parents with
//   `measure`; a newly created element starts with every flag set.
//
// Layout rules:
// - `update_layout` consumes `measure` and `arrange` work only; an unchanged
//   tree does no layout work.
// - Element bounds are relative to the parent, so moving an element does not
//   touch its subtree. An element whose size changes is marked `shape`.
// - Changing only `position` or `weight` re-arranges the parent without
//   measuring anything.
//
// Drawing rules:
// - `update_shapes` regenerates stored shapes only for elements marked
//   `shape`: new, resized or redecorated ones. Shapes are element-local, so
//   moving an element keeps them.
// - Elements clip their children to their own bounds unless disabled.
//
// Host rules:
// - The host owns the window, event loop and scheduling. It reports the
//   viewport and content scale, forwards input with `handle`, and draws only
//   when `needs_frame` says so, sleeping until input or `next_update`.
// - Any visible change (dirty elements, viewport, scale, clipping or an
//   explicit request) needs exactly one frame; `prepare_frame` consumes it.
//   Input on its own changes nothing visible.
class Context {
public:
    using Clock = std::chrono::steady_clock;

    Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    [[nodiscard]] ElementId root() const noexcept;
    [[nodiscard]] bool alive(ElementId element) const noexcept;
    [[nodiscard]] std::size_t element_count() const noexcept;

    // Appends a new element as the last child of `parent`.
    [[nodiscard]] base::Result<ElementId> create(ElementId parent);

    // Destroys `element` and its whole subtree. The root cannot be destroyed.
    [[nodiscard]] base::Status destroy(ElementId element);

    // Moves `element` under `parent` at `index` (append when empty). Moving
    // within the same parent reorders it. Cycles and the root are rejected.
    [[nodiscard]] base::Status reparent(ElementId element, ElementId parent,
                                        std::optional<std::size_t> index = std::nullopt);

    [[nodiscard]] base::Result<ElementId> parent(ElementId element) const;

    // Children in drawing order; empty for invalid handles.
    [[nodiscard]] std::span<const ElementId> children(ElementId element) const noexcept;

    [[nodiscard]] base::Status invalidate(ElementId element, Dirty flags);

    // Dirty flags of `element`; `Dirty::none` for invalid handles.
    [[nodiscard]] Dirty dirty(ElementId element) const noexcept;
    [[nodiscard]] bool has_dirty() const noexcept;

    // Live dirty elements in the order they first became dirty.
    [[nodiscard]] std::vector<ElementId> dirty_elements() const;

    // Clears the given flags on every element. Later update stages call this
    // after consuming their work.
    void clear_dirty(Dirty flags = Dirty::style | Dirty::measure | Dirty::arrange |
                                   Dirty::shape) noexcept;

    // Sets how `element` places its children; `nullptr` restores FreeLayout.
    [[nodiscard]] base::Status set_layout(ElementId element,
                                          std::shared_ptr<const Layout> layout);
    [[nodiscard]] base::Status set_layout_params(ElementId element,
                                                 const LayoutParams& params);
    [[nodiscard]] base::Result<LayoutParams> layout_params(ElementId element) const;

    // Lays out invalidated parts of the tree; the root fills `viewport`.
    LayoutStats update_layout(Size viewport);

    // Bounds from the last `update_layout`, relative to the parent's origin.
    [[nodiscard]] base::Result<Rect> bounds(ElementId element) const;
    // Bounds relative to the root's origin.
    [[nodiscard]] base::Result<Rect> absolute_bounds(ElementId element) const;

    // How the element's background is drawn; transparent by default.
    [[nodiscard]] base::Status set_decoration(ElementId element, const Decoration& decoration);
    [[nodiscard]] base::Result<Decoration> decoration(ElementId element) const;

    // Whether children are clipped to this element's bounds (default true).
    [[nodiscard]] base::Status set_clips_children(ElementId element, bool clips);
    [[nodiscard]] bool clips_children(ElementId element) const noexcept;

    // Replaces how the element's shapes are generated; `nullptr` restores
    // DecorationBuilder.
    [[nodiscard]] base::Status set_shape_builder(ElementId element,
                                                 std::shared_ptr<const ShapeBuilder> builder);

    // Replaces the element's shaped text. Its measured size contributes to the
    // element's intrinsic size; glyphs are drawn over its other shapes.
    [[nodiscard]] base::Status set_text(ElementId element, TextLayout layout,
                                        TextureId atlas,
                                        Color color = {1.0F, 1.0F, 1.0F, 1.0F});
    [[nodiscard]] base::Status clear_text(ElementId element);

    // Keeps a UTF-8 source for font-theme changes. Engine and atlas are borrowed
    // and must outlive the element; no font bytes or GPU resources are owned here.
    [[nodiscard]] base::Status set_text_source(ElementId element, TextEngine& engine,
                                               FontId font, std::string text,
                                               TextureId atlas);

    [[nodiscard]] base::Status set_theme_value(std::string name, ThemeValue value);
    [[nodiscard]] base::Result<ThemeValue> theme_value(std::string_view name) const;
    [[nodiscard]] base::Status bind_property(ElementId element, Property property,
                                              std::string name);
    [[nodiscard]] base::Status set_property_override(ElementId element, Property property,
                                                      ThemeValue value);
    // Resumes the bound theme value. An unbound property has no override to clear.
    [[nodiscard]] base::Status clear_property_override(ElementId element, Property property);
    [[nodiscard]] base::Result<ThemeValue> property(ElementId element, Property property) const;
    // Resolve before layout/shapes. Failure keeps unresolved work pending.
    [[nodiscard]] base::Result<StyleStats> update_styles();

    // Regenerates shapes of `shape`-marked elements from their current bounds.
    // Call after `update_layout`.
    ShapeStats update_shapes();

    // Shapes from the last `update_shapes`; null for invalid handles.
    [[nodiscard]] const ElementShapes* shapes(ElementId element) const noexcept;

    // Logical viewport and content scale (physical pixels per logical unit)
    // from the host. The scale must be finite and positive.
    [[nodiscard]] base::Status set_viewport(Size size, float scale = 1.0F);
    [[nodiscard]] Size viewport() const noexcept;
    [[nodiscard]] float scale() const noexcept;

    // Records host input and routes it through the current laid-out tree.
    // Input only requests a frame when a handler makes a visible change.
    base::Status handle(const InputEvent& event);
    [[nodiscard]] const InputState& input() const noexcept;

    [[nodiscard]] base::Status set_interaction(ElementId element, Interaction interaction);
    [[nodiscard]] base::Result<Interaction> interaction(ElementId element) const;
    [[nodiscard]] base::Status set_event_handler(ElementId element, EventHandler handler);
    // Routes snapshot the ancestor chain. Callbacks may mutate the tree;
    // removed recipients are skipped. Hover/focus/capture notifications are
    // direct, while host input bubbles. Tab traverses unless prevent_default().
    // Topmost laid-out element, respecting ancestor clips and disabled subtrees.
    [[nodiscard]] ElementId hit_test(Point position) const;
    [[nodiscard]] ElementId hovered() const noexcept;
    [[nodiscard]] ElementId focused() const noexcept;
    [[nodiscard]] base::Status focus(ElementId element); // empty clears focus
    base::Status focus_next(bool backwards = false);
    [[nodiscard]] base::Status capture_pointer(ElementId element, std::uint32_t pointer = 0);
    base::Status release_pointer(std::uint32_t pointer = 0);
    [[nodiscard]] ElementId captured(std::uint32_t pointer = 0) const noexcept;
    // No log storage or trace work when the optional sink is empty.
    void set_event_trace(EventTrace trace);
    [[nodiscard]] const base::Status& last_event_status() const noexcept;

    // Translates children for scrolling without changing layout or stored shapes.
    [[nodiscard]] base::Status set_child_offset(ElementId element, Point offset);
    [[nodiscard]] Point child_offset(ElementId element) const noexcept;

    enum class Role : std::uint8_t { generic, panel, label, image, button, checkbox,
                                    slider, scroll_area };
    struct Semantics {
        Role role = Role::generic;
        std::string label;
        friend bool operator==(const Semantics&, const Semantics&) = default;
    };
    [[nodiscard]] base::Status set_semantics(ElementId element, Semantics semantics);
    [[nodiscard]] base::Result<Semantics> semantics(ElementId element) const;

    // Asks for a frame now (for changes the GUI cannot see) or at `when`
    // (timed updates such as animations or caret blinking).
    void request_frame() noexcept;
    void request_frame_at(Clock::time_point when);

    [[nodiscard]] bool needs_frame(Clock::time_point now) const noexcept;
    // Earliest pending timed update, if any.
    [[nodiscard]] std::optional<Clock::time_point> next_update() const noexcept;

    // Resolves styles then brings layout and shapes up to date. Check status
    // before drawing; failed resolution leaves work pending. Success consumes
    // the frame request, every dirty flag and timed updates that are due.
    FrameStats prepare_frame(Clock::time_point now);

    // Platform services; `nullptr` restores a host that does nothing. The host
    // is borrowed and must outlive its use here.
    void set_host(Host* host) noexcept;
    [[nodiscard]] Host& host() const noexcept;

private:
    friend class LayoutPass;

    struct Node {
        struct StyleBinding {
            std::string name;
            std::optional<ThemeValue> override_value;
            std::optional<ThemeValue> resolved;
        };
        struct TextSource {
            TextEngine* engine = nullptr;
            std::string text;
            TextureId atlas;
        };
        std::uint32_t generation = 1;
        bool alive = false;
        bool queued = false;
        bool measured = false;
        bool arranged = false;
        Dirty dirty = Dirty::none;
        ElementId parent;
        std::vector<ElementId> children;
        std::shared_ptr<const Layout> layout;
        LayoutParams params;
        Constraints measured_under;
        Size desired;
        Rect rect;
        bool clips = true;
        Decoration decoration;
        std::shared_ptr<const ShapeBuilder> builder;
        std::optional<TextBlock> text;
        ElementShapes shapes;
        Interaction interaction;
        EventHandler handler;
        Point child_offset;
        Semantics semantics;
        std::array<StyleBinding, static_cast<std::size_t>(Property::count)> styles;
        FontId font;
        Color text_color{1, 1, 1, 1};
        std::optional<TextSource> text_source;
    };

    [[nodiscard]] Node* find(ElementId element) noexcept;
    [[nodiscard]] const Node* find(ElementId element) const noexcept;
    [[nodiscard]] ElementId allocate();
    void mark(ElementId element, Dirty flags);
    // Sets exactly `flags` on one element, without implied flags or ancestors.
    void raise(ElementId element, Node& node, Dirty flags);
    void detach(ElementId element, Node& node);
    [[nodiscard]] bool input_enabled(ElementId element) const noexcept;
    base::Result<bool> dispatch(ElementId target, RoutedPayload payload, bool bubble = true);
    void route_input(const InputEvent& event);
    void update_hover(ElementId next);
    void forget_input(ElementId element) noexcept;
    void forget_styles(ElementId element);
    void mark_style(ElementId element, Property property);

    [[nodiscard]] const LayoutParams& params_of(ElementId element) const noexcept;
    [[nodiscard]] Size desired_of(ElementId element) const noexcept;
    Size measure_element(ElementId element, Constraints constraints);
    void arrange_element(ElementId element, Rect rect);

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_;
    std::vector<ElementId> dirty_queue_;
    std::size_t live_ = 0;
    ElementId root_;
    Size viewport_;
    LayoutStats stats_;
    Size host_viewport_;
    float scale_ = 1.0F;
    bool frame_requested_ = false;
    std::vector<Clock::time_point> deadlines_;
    InputState input_;
    Host* host_ = nullptr;
    ElementId hovered_;
    ElementId focused_;
    std::unordered_map<std::uint32_t, ElementId> captures_;
    std::unordered_map<std::uint32_t, std::uint8_t> pointer_buttons_;
    EventTrace trace_;
    base::Status event_status_;
    struct ThemeDependent { ElementId element; Property property; };
    std::unordered_map<std::string, ThemeValue> theme_;
    std::unordered_map<std::string, std::vector<ThemeDependent>> theme_dependents_;
};

} // namespace truffle::gui

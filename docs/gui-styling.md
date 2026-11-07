# GUI styling (#280)

Truffle supplies defaults for its GUI components. An application can change
shared values, adjust one kind of component everywhere, or override an
individual component. Layout is separate: it places elements and chooses how
they share space.

## Layers

For each styled value, Truffle resolves:

1. Truffle's component default.
2. The application's sparse override for that `Component`.
3. A sparse override for a particular component element.
4. An optional direct property override on the actual element.

Each `set_component_style` call **replaces** the sparse override at its level.
Read it with `component_style`, edit it and set it again to retain other
overrides. An empty `ComponentStyle{}` resets that level. `component_defaults`
returns Truffle's defaults; `component_property` resolves the component
layers, while `property` returns the value applied to an element after style
resolution. Direct property overrides target that element only. Use the
component override to style a composite widget's internal parts too.

Color values can have normal, hovered, pressed, focused, disabled and checked
entries. Active states are considered in disabled, pressed, hovered, checked,
focused, then normal order; the first state with a value wins. Within that
state, per-element overrides win over global overrides and defaults. Therefore
overriding a normal color does not remove inherited hover or pressed feedback.
If a state has no value, resolution continues to the next active state.

```cpp
gui::ComponentStyle buttons;
buttons.corner_radius = 6.0F;
buttons.min_size = gui::Size{88, 32};
buttons.fill.normal = gui::Color{0.20F, 0.30F, 0.45F, 1};
buttons.fill.hovered = gui::ColorToken::hover;
check(context.set_component_style(gui::Component::button, buttons));

gui::ComponentStyle special;
special.fill.normal = gui::Color{0.55F, 0.10F, 0.10F, 1};
check(context.set_component_style(button.element(), special));
check(context.set_component_style(button.element(), {})); // inherit buttons
```

`check` represents the application's normal error handling for `base::Status`.
Token references stay live when their shared values change. Existing named
theme bindings and direct `set_property_override`/`clear_property_override`
remain available for individual elements.

## Shared tokens and palettes

Use the typed `Context::set_theme_value` and `theme_value` overloads:

| Type | Tokens |
| --- | --- |
| `ColorToken` | `surface`, `control`, `hover`, `pressed`, `disabled`, `accent`, `border`, `text`, `text_disabled`, `image`, `selection`, `caret`, `composition`; `dock_background`, `dock_area`, `dock_tab`, `dock_tab_active`, `dock_divider`, `dock_marker`, `dock_marker_border` |
| `MetricToken` | `radius`, `border_width`, `dock_marker_border_width`, `dock_radius`, `dock_tab_radius` |
| `PaddingToken` | `standard` |
| `FontSizeToken` | `standard` |

`apply_theme(context, BuiltinTheme::truffle_dark)` or `truffle_light` selects
the built-in neutral **color palette**. It fills any missing defaults, but
does not reset existing non-color values or component overrides. Literal
colors in component overrides remain literal; token references follow the
new palette. `install_default_theme` only fills missing values, using Dark.
The string-based names such as `gui.surface`, `gui.padding` and `dock.tab`
remain available for low-level bindings.

## Component properties

`ComponentStyle` provides stateful `fill`, `border`, `text`, `indicator`,
`indicator_border`, `selection`, `caret` and `composition` colors; `border_width`,
`corner_radius`, `padding`, `min_size` and `font_size`; and component-specific
geometry. Each component consumes the properties it actually draws or uses:
for example, checkbox and menu indicators use `indicator`, text editors use
selection/caret/composition, and a slider uses its border color for its track
and thumb. A component does not draw an unused part merely because it has
been assigned a style.

| Component | Default sizing and specific geometry, in logical units |
| --- | --- |
| Button, checkbox | Minimum 80 x 32; checkbox indicator 16 x 16, gap 8 |
| Slider | Minimum 120 x 24; track thickness 4, thumb width 12, inset 2 |
| Single-line / multiline text edit | Minimum 120 x 30 / 120 x 80; padding 8, 6, 8, 6 |
| Menu | Minimum width 160; standard padding |
| Menu button | Minimum 32 x 28; indicator 12 x 12, trailing column 12, gap 6 |
| Menu item | Minimum height 28; padding 4, 6, 4, 6; indicator 12 x 12, leading column 16, trailing column 12, gap 6 |
| Menu separator | Minimum height 9 |

Use `style.checkbox`, `style.slider` or `style.menu` to change those specific
metrics. Menu leading width applies to items, not buttons. Checkbox padding
normally includes its indicator width and gap; an explicit component padding
replaces that derived padding, so include any desired indicator inset.
Optional Dock components use the same style layers; `DockMetrics` separately
owns docking layout and interaction geometry.

## Layout

Width, height, position, row/column spacing and grid placement belong to
`LayoutParams` and layouts, not the theme. A fixed axis or nonzero authored
minimum overrides that axis's styled minimum; the authored maximum clamps a
styled minimum. Set a component's `min_size` to zero to remove its default
minimum. Nonzero padding authored through `LayoutParams` takes precedence over
component padding; a direct padding property override takes precedence over
both. Set an explicit zero padding through a component or property override.
Reading and updating `layout_params` without changing its effective padding
does not freeze the inherited padding. There is no margin model or CSS
selector/subtree cascade.

## Fonts

The host chooses licensed font bytes and fallback fonts. `TextEngine::load_font`
and `Graphics::load_font` default to `gui::default_font_size` (14 logical
units), and accept an explicit size. Truffle does not discover or bundle a
font. A default `FontSize{}` uses the host-loaded font's existing size; set
`FontSizeToken::standard` or a component's `font_size` to `FontSize{units}`
to request another size.

The text engine caches sized variants and their fallback faces. Variants are
engine-owned: do not unload them or edit their fallback chains directly.
Changing the source's fallbacks updates its variants, and unloading the source
invalidates them. A face that cannot provide the requested size (for example,
a fixed-size bitmap font) reports an error instead of silently resizing.
Contexts borrowing text engines and atlases must not outlive those owners.

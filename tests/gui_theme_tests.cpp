#include "truffle/gui/specialist/text.hpp"
#include "gui_text_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/themes.hpp"
#include "truffle/gui/widgets.hpp"

#include <limits>

namespace {
using namespace truffle;
using namespace truffle::gui;

int dependencies() {
    Context c;
    const auto a = c.create(c.root()).value();
    const auto b = c.create(c.root()).value();
    const auto unrelated = c.create(c.root()).value();
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    TFL_CHECK(c.set_theme_value("accent", Color{1, 0, 0, 1}).ok());
    TFL_CHECK(c.set_theme_value("padding", Edges{2, 2, 2, 2}).ok());
    TFL_CHECK(c.bind_property(a, Property::fill, "accent").ok());
    TFL_CHECK(c.bind_property(b, Property::fill, "accent").ok());
    TFL_CHECK(c.bind_property(a, Property::padding, "padding").ok());
    auto first = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(first.status.ok() && first.styles.resolved == 2);
    TFL_CHECK((c.decoration(a).value().fill == Color{1, 0, 0, 1}));
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).styles.resolved == 0);
    TFL_CHECK(c.set_theme_value("accent", Color{0, 1, 0, 1}).ok());
    TFL_CHECK(c.dirty(a) == (Dirty::style | Dirty::shape));
    TFL_CHECK(c.dirty(b) == (Dirty::style | Dirty::shape));
    TFL_CHECK(c.dirty(c.root()) == Dirty::none && c.dirty(unrelated) == Dirty::none);
    auto changed = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(changed.status.ok() && changed.styles.resolved == 2);
    TFL_CHECK(changed.layout.measured == 0 && changed.layout.arranged == 0 &&
              changed.shapes.regenerated == 2);
    TFL_CHECK(c.set_property_override(a, Property::fill, Color{0, 0, 1, 1}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).styles.resolved == 1);
    TFL_CHECK(c.set_theme_value("accent", Color{1, 1, 0, 1}).ok());
    TFL_CHECK(c.dirty(a) == Dirty::none && c.dirty(b) == (Dirty::style | Dirty::shape));
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).styles.resolved == 1);
    TFL_CHECK(c.clear_property_override(a, Property::fill).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).styles.resolved == 1);
    TFL_CHECK((c.decoration(a).value().fill == Color{1, 1, 0, 1}));
    TFL_CHECK(c.set_theme_value("accent", Color{1, 1, 0, 1}).ok());
    TFL_CHECK(!c.has_dirty());
    TFL_CHECK(c.set_theme_value("padding", Edges{4, 4, 4, 4}).ok());
    TFL_CHECK(has(c.dirty(a), Dirty::measure));
    TFL_CHECK(c.dirty(c.root()) == (Dirty::measure | Dirty::arrange));
    TFL_CHECK(c.dirty(b) == Dirty::none && c.dirty(unrelated) == Dirty::none);
    changed = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(changed.status.ok() && changed.styles.resolved == 1 &&
              changed.layout.measured == 2);
    TFL_CHECK(!c.bind_property(a, Property::fill, "missing").ok());
    TFL_CHECK(!c.bind_property(a, Property::padding, "accent").ok());
    TFL_CHECK(!c.set_theme_value("accent", 2.0F).ok());
    TFL_CHECK(!c.set_theme_value("bad", std::numeric_limits<float>::quiet_NaN()).ok());
    TFL_CHECK(!c.set_property_override(a, Property::corner_radius, -1.0F).ok());
    TFL_CHECK(!c.set_property_override(a, Property::count, 1.0F).ok());
    TFL_CHECK(c.set_theme_value("other", Color{1, 1, 1, 1}).ok());
    TFL_CHECK(c.bind_property(a, Property::fill, "other").ok());
    static_cast<void>(c.prepare_frame(Context::Clock::now()));
    TFL_CHECK(c.destroy(b).ok());
    static_cast<void>(c.prepare_frame(Context::Clock::now()));
    TFL_CHECK(c.set_theme_value("accent", Color{1, 0, 1, 1}).ok());
    TFL_CHECK(!c.has_dirty()); // rebound and destroyed subscriptions removed
    TFL_CHECK(c.set_layout(a, std::make_shared<StackLayout>()).ok());
    TFL_CHECK(c.set_theme_value("gap", 5.0F).ok());
    TFL_CHECK(c.bind_property(a, Property::spacing, "gap").ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(std::get<float>(c.property(a, Property::spacing).value()) == 5);
    return 0;
}

int component_styles() {
    Context c;
    TFL_CHECK(c.set_viewport({400, 200}).ok());
    const auto first = tests::require(Widget::create(c, c.root(), {.kind = WidgetKind::button}));
    const auto second = tests::require(Widget::create(c, c.root(), {.kind = WidgetKind::button}));
    const auto other = tests::require(Widget::create(c, c.root()));
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());

    const Color red{1, 0, 0, 1};
    const Color blue{0, 0, 1, 1};
    const Color green{0, 1, 0, 1};
    ComponentStyle global;
    global.fill.normal = red;
    global.border_width = 3.0F;
    global.min_size = Size{110, 40};
    global.padding = Edges{3, 4, 3, 4};
    TFL_CHECK(c.set_component_style(Component::button, global).ok());
    TFL_CHECK(c.dirty(other.element()) == Dirty::none);
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill == red);
    TFL_CHECK(c.decoration(second.element()).value().fill == red);
    TFL_CHECK(std::get<float>(c.property(first.element(), Property::border_width).value()) == 3);
    TFL_CHECK((std::get<Size>(c.property(first.element(), Property::min_size).value()) ==
               Size{110, 40}));

    ComponentStyle local;
    local.fill.normal = blue;
    local.fill.pressed = green;
    local.corner_radius = 9.0F;
    TFL_CHECK(c.set_component_style(first.element(), local).ok());
    TFL_CHECK(c.dirty(second.element()) == Dirty::none);
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill == blue);
    TFL_CHECK(c.decoration(second.element()).value().fill == red);
    TFL_CHECK(c.set_component_state(first.element(), {.hovered = true}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill == c.theme_value(ColorToken::hover).value());
    TFL_CHECK(c.set_component_state(first.element(), {.hovered = true, .pressed = true}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill == green);
    TFL_CHECK(c.set_component_state(first.element(),
                                    {.hovered = true, .pressed = true, .disabled = true}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill ==
              c.theme_value(ColorToken::disabled).value());
    TFL_CHECK(c.set_component_state(first.element(), {}).ok());

    TFL_CHECK(c.set_theme_value(MetricToken::radius, 7.0F).ok());
    TFL_CHECK(c.set_theme_value(PaddingToken::standard, Edges{11, 12, 11, 12}).ok());
    TFL_CHECK(c.set_theme_value(FontSizeToken::standard, FontSize{18.0F}).ok());
    const auto dark_surface = c.theme_value(ColorToken::surface).value();
    TFL_CHECK(apply_theme(c, BuiltinTheme::truffle_light).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.theme_value(ColorToken::surface).value() != dark_surface);
    TFL_CHECK(c.theme_value(MetricToken::radius).value() == 7);
    TFL_CHECK((c.theme_value(PaddingToken::standard).value() == Edges{11, 12, 11, 12}));
    TFL_CHECK((c.theme_value(FontSizeToken::standard).value() == FontSize{18.0F}));
    TFL_CHECK(c.component_style(Component::button).value() == global);
    TFL_CHECK(c.component_style(first.element()).value() == local);
    TFL_CHECK(c.decoration(first.element()).value().fill == blue);
    TFL_CHECK(c.decoration(second.element()).value().fill == red);

    ComponentStyle invalid = global;
    invalid.slider.thumb_width = 4;
    TFL_CHECK(!c.set_component_style(Component::button, invalid).ok());
    TFL_CHECK(c.component_style(Component::button).value() == global);
    invalid = {};
    invalid.menu.leading_width = 4;
    TFL_CHECK(!c.set_component_style(Component::menu_button, invalid).ok());
    invalid = {};
    invalid.font_size = FontSize{0};
    TFL_CHECK(!c.set_component_style(first.element(), invalid).ok());
    TFL_CHECK(c.component_style(first.element()).value() == local);
    TFL_CHECK(!c.set_theme_value(ColorToken::count, red).ok());
    TFL_CHECK(!c.set_theme_value(MetricToken::radius, -1.0F).ok());
    TFL_CHECK(!c.set_theme_value(FontSizeToken::standard,
                                 FontSize{std::numeric_limits<float>::quiet_NaN()}).ok());

    TFL_CHECK(c.set_component_style(first.element(), {}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill == red);
    TFL_CHECK(c.set_component_style(Component::button, {}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.decoration(first.element()).value().fill ==
              c.theme_value(ColorToken::control).value());
    TFL_CHECK((c.component_defaults(Component::button).value().min_size == Size{80, 32}));
    TFL_CHECK(c.destroy(first.element()).ok());
    TFL_CHECK(c.set_component_style(Component::button, global).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(!c.component_style(first.element()).ok());
    return 0;
}

int style_layout_precedence() {
    Context c;
    TFL_CHECK(c.set_viewport({300, 200}).ok());
    const auto button = tests::require(Widget::create(c, c.root(), {.kind = WidgetKind::button}));
    const auto id = button.element();
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.layout_params(id).value().padding == Edges{8, 8, 8, 8}));

    LayoutParams params;
    params.width = 30;
    params.height = 15;
    TFL_CHECK(c.set_layout_params(id, params).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.bounds(id).value().size() == Size{30, 15}));
    TFL_CHECK((c.layout_params(id).value().padding == Edges{8, 8, 8, 8}));

    params.padding = {2, 3, 4, 5};
    TFL_CHECK(c.set_layout_params(id, params).ok());
    ComponentStyle global;
    global.padding = Edges{12, 12, 12, 12};
    TFL_CHECK(c.set_component_style(Component::button, global).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.layout_params(id).value().padding == Edges{2, 3, 4, 5}));
    TFL_CHECK(c.set_property_override(id, Property::padding, Edges{12, 12, 12, 12}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.layout_params(id).value().padding == Edges{12, 12, 12, 12}));
    TFL_CHECK(c.clear_property_override(id, Property::padding).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.layout_params(id).value().padding == Edges{2, 3, 4, 5}));

    params.padding = {};
    TFL_CHECK(c.set_layout_params(id, params).ok());
    ComponentStyle local;
    local.padding = Edges{};
    local.min_size = Size{100, 50};
    TFL_CHECK(c.set_component_style(id, local).ok());
    params.width.reset();
    params.height.reset();
    TFL_CHECK(c.set_layout_params(id, params).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.bounds(id).value().size() == Size{100, 50}));
    params.min = {40, 20};
    TFL_CHECK(c.set_layout_params(id, params).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.bounds(id).value().size() == Size{40, 20}));
    params.min = {};
    params.max = {70, 30};
    TFL_CHECK(c.set_layout_params(id, params).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.bounds(id).value().size() == Size{70, 30}));
    local.min_size = Size{};
    TFL_CHECK(c.set_component_style(id, local).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.bounds(id).value().size() == Size{}));
    return 0;
}

int style_handler_retry() {
    Context c;
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    const auto later = tests::require(c.create(c.root()));
    const auto trigger = tests::require(c.create(c.root()));
    for (const auto element : {later, trigger}) {
        TFL_CHECK(c.set_component(element, Component::button).ok());
        TFL_CHECK(c.bind_component_property(element, Property::fill, element).ok());
    }
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    bool fail = true;
    TFL_CHECK(c.set_component_style_handler(trigger, [&] {
        if (fail) {
            fail = false;
            return base::Status::failure(base::StatusCode::invalid_state, "retry style");
        }
        return c.set_property_override(later, Property::fill, Color{0, 1, 0, 1});
    }).ok());
    TFL_CHECK(c.set_property_override(trigger, Property::fill, Color{1, 0, 0, 1}).ok());
    TFL_CHECK(!c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.decoration(later).value().fill == Color{0, 1, 0, 1}));
    return 0;
}

int fonts() {
#if TFL_TEST_GUI_TEXT
    auto device = tests::make_null_context();
    auto engine = tests::require(specialist::TextInterop::create(device.device, {{64, 64}}));
    const auto small = tests::require(engine.load_font(tests::gui_test_font(), 7));
    const auto font_bytes = tests::gui_test_font();
    std::string wider(reinterpret_cast<const char*>(font_bytes.data()), font_bytes.size());
    for (std::size_t at = wider.find("DWIDTH 5"); at != std::string::npos;
         at = wider.find("DWIDTH 5", at + 1)) {
        wider.replace(at, 8, "DWIDTH 8");
    }
    const auto large = tests::require(engine.load_font(
        std::as_bytes(std::span{wider.data(), wider.size()}), 7));
    Context c;
    const auto label = c.create(c.root()).value();
    const auto sibling = c.create(c.root()).value();
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    TFL_CHECK(c.set_theme_value("font", small).ok());
    TFL_CHECK(c.set_theme_value("ink", Color{0, 1, 1, 1}).ok());
    TFL_CHECK(c.bind_property(label, Property::font, "font").ok());
    TFL_CHECK(c.bind_property(label, Property::text_color, "ink").ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.set_text_source(label, engine, small, "A", {1}).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    const auto width = c.bounds(label).value().width;
    TFL_CHECK(c.shapes(label)->vertices.front().fill[1] == 1);
    TFL_CHECK(c.set_theme_value("font", large).ok());
    TFL_CHECK(c.dirty(sibling) == Dirty::none);
    const auto changed = c.prepare_frame(Context::Clock::now());
    TFL_CHECK(changed.status.ok() && changed.styles.resolved == 1 &&
              changed.layout.measured == 2 && changed.shapes.regenerated == 1);
    TFL_CHECK(c.bounds(label).value().width > width);
    TFL_CHECK(engine.unload_font(small).ok());
    TFL_CHECK(c.set_theme_value("font", small).ok());
    TFL_CHECK(!c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK(c.needs_frame(Context::Clock::now()));
    TFL_CHECK(c.set_theme_value("font", large).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
#endif
    return 0;
}
} // namespace

int builtin_themes() {
    TFL_CHECK(builtin_theme_name(BuiltinTheme::truffle_dark) == "Truffle Dark");
    TFL_CHECK(builtin_theme_name(BuiltinTheme::truffle_light) == "Truffle Light");
    const auto colour = [](const Context& c, std::string_view name) {
        return std::get<Color>(c.theme_value(name).value());
    };
    const char* names[] = {"gui.surface", "gui.control", "gui.hover", "gui.pressed",
                           "gui.disabled", "gui.accent", "gui.border", "gui.text",
                           "gui.selection", "gui.caret", "gui.composition",
                           "dock.background", "dock.area", "dock.tab", "dock.tab_active",
                           "dock.divider", "dock.marker", "dock.marker_border"};
    for (const auto theme : {BuiltinTheme::truffle_dark, BuiltinTheme::truffle_light}) {
        Context c;
        TFL_CHECK(apply_theme(c, theme).ok());
        for (const char* name : names) {
            const Color value = colour(c, name);
            TFL_CHECK(value.r == value.g && value.g == value.b); // neutral greys only
        }
        const bool dark = theme == BuiltinTheme::truffle_dark;
        TFL_CHECK((colour(c, "gui.text").r > colour(c, "gui.surface").r) == dark);
    }

    // Defaults fill gaps only; application values survive.
    Context c;
    TFL_CHECK(c.set_theme_value("gui.accent", Color{1, 0, 0, 1}).ok());
    TFL_CHECK(install_default_theme(c).ok());
    TFL_CHECK((colour(c, "gui.accent") == Color{1, 0, 0, 1}));
    TFL_CHECK((colour(c, "gui.surface") == Color{0.12F, 0.12F, 0.12F, 1}));

    // Switching repaints bound widgets.
    TFL_CHECK(c.set_viewport({100, 100}).ok());
    auto panel = Widget::create(c, c.root(), {.kind = WidgetKind::panel});
    TFL_CHECK(panel.ok());
    const auto element = panel.value().element();
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.decoration(element).value().fill == Color{0.12F, 0.12F, 0.12F, 1}));
    TFL_CHECK(apply_theme(c, BuiltinTheme::truffle_light).ok());
    TFL_CHECK(c.prepare_frame(Context::Clock::now()).status.ok());
    TFL_CHECK((c.decoration(element).value().fill == Color{0.96F, 0.96F, 0.96F, 1}));
    TFL_CHECK((colour(c, "gui.accent") == Color{0.22F, 0.22F, 0.22F, 1}));

    // A conflicting value type is rejected before any token changes.
    Context mismatch;
    TFL_CHECK(mismatch.set_theme_value("gui.radius", Color{1, 1, 1, 1}).ok());
    TFL_CHECK(!apply_theme(mismatch, BuiltinTheme::truffle_dark).ok());
    TFL_CHECK(!mismatch.theme_value("gui.surface").ok());
    return 0;
}

int main() {
    if (dependencies() != 0 || builtin_themes() != 0 || component_styles() != 0 ||
        style_layout_precedence() != 0 || style_handler_retry() != 0) { return 1; }
    return fonts();
}

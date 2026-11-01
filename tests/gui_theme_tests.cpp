#include "gui_text_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/context.hpp"

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

int fonts() {
#if TFL_TEST_GUI_TEXT
    auto device = tests::make_null_context();
    auto engine = tests::require(TextEngine::create(device.device, {{64, 64}}));
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

int main() {
    if (dependencies() != 0) { return 1; }
    return fonts();
}

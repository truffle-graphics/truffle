#include "gui_text_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/text.hpp"

#include <array>
#include <span>
#include <utility>

namespace {

using namespace truffle;

int test_optional_support() {
    auto context = tests::make_null_context();
    const auto engine = gui::TextEngine::create(context.device);
    gui::TextEngine empty;
    const auto empty_atlas = empty.atlas_texture();
    TFL_CHECK(!empty_atlas.ok());
#if TFL_TEST_GUI_TEXT
    TFL_CHECK(gui::TextEngine::available());
    TFL_CHECK(engine.ok());
    TFL_CHECK(empty_atlas.status().code == base::StatusCode::invalid_state);
    TFL_CHECK(empty.last_stats().shaped_glyphs == 0);
#else
    TFL_CHECK(!gui::TextEngine::available());
    TFL_CHECK(!engine.ok());
    TFL_CHECK(engine.status().code == base::StatusCode::unsupported);
    TFL_CHECK(empty_atlas.status().code == base::StatusCode::unsupported);
#endif
    return 0;
}

#if TFL_TEST_GUI_TEXT
int test_moved_from_engine() {
    auto context = tests::make_null_context();
    auto engine = tests::require(gui::TextEngine::create(context.device, {{64, 64}}));
    TFL_CHECK(engine.atlas_texture().ok());
    gui::TextEngine moved = std::move(engine);
    TFL_CHECK(moved.atlas_texture().ok());
    // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from contract is under test.
    const auto stale = engine.atlas_texture();
    TFL_CHECK(!stale.ok());
    TFL_CHECK(stale.status().code == base::StatusCode::invalid_state);
    TFL_CHECK(!engine.load_font(tests::gui_test_font(), 7.0F).ok());
    return 0;
}
int test_shape_atlas_and_invalidation() {
    auto context = tests::make_null_context();
    auto engine = tests::require(gui::TextEngine::create(context.device, {{64, 64}}));
    const auto bytes = tests::gui_test_font();
    const gui::FontId font =
        tests::require(engine.load_font(bytes, 7.0F));

    auto layout = tests::require(engine.shape(font, "A A"));
    TFL_CHECK(layout.glyphs.size() == 3);
    TFL_CHECK(layout.glyphs[0].cluster == 0);
    TFL_CHECK(layout.glyphs[1].cluster == 1);
    TFL_CHECK(layout.glyphs[2].cluster == 2);
    TFL_CHECK(layout.glyphs[0].size.width > 0.0F);
    TFL_CHECK(layout.glyphs[0].size.height > 0.0F);
    TFL_CHECK(layout.glyphs[1].size.width == 0.0F);
    TFL_CHECK(layout.size.width > 0.0F);
    TFL_CHECK(layout.size.height > 0.0F);
    TFL_CHECK(engine.last_stats().shaped_glyphs == 3);
    TFL_CHECK(engine.last_stats().rasterized_glyphs == 1);
    TFL_CHECK(engine.last_stats().atlas_updates == 1);

    static_cast<void>(tests::require(engine.shape(font, "A A")));
    TFL_CHECK(engine.last_stats().rasterized_glyphs == 0);
    TFL_CHECK(engine.last_stats().atlas_updates == 0);

    gui::Context tree;
    const auto first = tests::require(tree.create(tree.root()));
    const auto second = tests::require(tree.create(tree.root()));
    TFL_CHECK(tree.set_text(first, layout, gui::TextureId{1}).ok());
    const auto measured = tree.update_layout({80.0F, 30.0F});
    TFL_CHECK(measured.measured == 3);
    TFL_CHECK(tree.bounds(first).value().size() == layout.size);
    static_cast<void>(tree.update_shapes());
    TFL_CHECK(tree.shapes(first)->vertices.size() == 8);
    TFL_CHECK(tree.shapes(second)->empty());

    // A color change repaints only this element; it does not remeasure it or
    // the unaffected sibling.
    TFL_CHECK(tree.set_text(first, layout, gui::TextureId{1}, {1, 0, 0, 1}).ok());
    TFL_CHECK(!gui::has(tree.dirty(first), gui::Dirty::measure));
    TFL_CHECK(tree.update_layout({80.0F, 30.0F}).measured == 0);
    TFL_CHECK(tree.update_shapes().regenerated == 1);

    auto shorter = tests::require(engine.shape(font, "A"));
    TFL_CHECK(tree.set_text(first, std::move(shorter), gui::TextureId{1}).ok());
    TFL_CHECK(tree.update_layout({80.0F, 30.0F}).measured == 2);
    TFL_CHECK(tree.bounds(first).value().width < layout.size.width);
    TFL_CHECK(tree.update_shapes().regenerated == 1);
    TFL_CHECK(tree.clear_text(first).ok());
    TFL_CHECK(tree.update_layout({80.0F, 30.0F}).measured == 2);
    TFL_CHECK(tree.bounds(first).value().size() == gui::Size{});

    TFL_CHECK(engine.unload_font(font).ok());
    const auto stale = engine.shape(font, "A");
    TFL_CHECK(!stale.ok());
    TFL_CHECK(stale.status().code == base::StatusCode::invalid_argument);
    return 0;
}
#endif

} // namespace

int main() {
    if (const int result = test_optional_support(); result != 0) {
        return result;
    }
#if TFL_TEST_GUI_TEXT
    if (const int result = test_moved_from_engine(); result != 0) {
        return result;
    }
    return test_shape_atlas_and_invalidation();
#else
    return 0;
#endif
}

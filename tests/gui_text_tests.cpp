#include "gui_text_fixture.hpp"
#include "gui_unicode_fixture.hpp"
#include "renderer_native_test_utils.hpp"
#include "rhi_test_utils.hpp"
#include "test_support.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/text.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

#if TFL_TEST_GUI_TEXT
#include "gui_unicode_conformance.hpp"
#endif

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
    TFL_CHECK(gui::text_graphemes("A").status().code == base::StatusCode::unsupported);
    TFL_CHECK(empty.set_fallbacks({}, {}).code == base::StatusCode::unsupported);
#endif
    return 0;
}

#if TFL_TEST_GUI_TEXT
int test_generated_ligature_and_script_shaping() {
    auto context = tests::make_null_context();
    auto engine = tests::require(gui::TextEngine::create(context.device, {{256, 256}}));
    const auto font = tests::require(engine.load_font(tests::gui_ligature_font(), 20));
    const auto ligature = tests::require(engine.shape(font, "fi"));
    TFL_CHECK(ligature.glyphs.size() == 1 && ligature.glyphs[0].id == 6);
    TFL_CHECK(ligature.carets.size() == 3);
    TFL_CHECK(ligature.caret_position(0).x == 0);
    TFL_CHECK(ligature.caret_position(1).x == 4); // Original GDEF caret, not midpoint.
    TFL_CHECK(ligature.caret_position(2).x == 18);
    TFL_CHECK(ligature.clusters.size() == 2);
    TFL_CHECK(ligature.selection_rects(0, 1)[0].width == 4);
    TFL_CHECK(ligature.selection_rects(1, 2)[0].width == 14);
    const auto composed = tests::require(engine.shape(font, "e\xcc\x81"));
    TFL_CHECK(composed.glyphs.size() == 1 && composed.glyphs[0].id == 9);
    TFL_CHECK(composed.carets.size() == 2);
    const auto isolated = tests::require(engine.shape(font, "\xd8\xa8"));
    TFL_CHECK(isolated.glyphs.size() == 1 && isolated.glyphs[0].id == 11);
    const auto script = tests::require(engine.shape(font, "A \xd8\xa8\xd8\xa8\xd8\xa8 A"));
    TFL_CHECK(script.glyphs.size() == 7);
    TFL_CHECK(script.glyphs[2].id == 14);
    TFL_CHECK(script.glyphs[3].id == 13);
    TFL_CHECK(script.glyphs[4].id == 12);

    const auto primary = tests::require(engine.load_font(tests::gui_test_font(), 7));
    const auto second = tests::require(engine.load_font(tests::gui_ligature_font(), 20));
    TFL_CHECK(engine.set_fallbacks(primary, std::array{font, second}).ok());
    const auto fallback = tests::require(engine.shape(primary, "fi"));
    TFL_CHECK(fallback.glyphs.size() == 1 && fallback.glyphs[0].font == font);
    TFL_CHECK(fallback.line_height >= 20);
    TFL_CHECK(engine.set_fallbacks(primary, std::array{second, font}).ok());
    const auto reordered = tests::require(engine.shape(primary, "fi"));
    TFL_CHECK(reordered.glyphs.size() == 1 && reordered.glyphs[0].font == second);
    const auto only_base = tests::require(engine.load_font(tests::gui_unicode_font({0x65}), 7));
    TFL_CHECK(engine.set_fallbacks(only_base, std::array{font}).ok());
    const auto whole = tests::require(engine.shape(only_base, "e\xcc\x81"));
    TFL_CHECK(whole.glyphs.size() == 1 && whole.glyphs[0].font == font);
    return 0;
}

int test_unicode_boundaries() {
    TFL_CHECK(tests::require(gui::text_graphemes("")) == std::vector<std::size_t>{0});
    TFL_CHECK((tests::require(gui::text_graphemes("e\xcc\x81")) ==
               std::vector<std::size_t>{0, 3}));
    TFL_CHECK((tests::require(gui::text_graphemes("A\r\nB")) ==
               std::vector<std::size_t>{0, 1, 3, 4}));
    const std::string family = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9"
                               "\xe2\x80\x8d\xf0\x9f\x91\xa7";
    TFL_CHECK((tests::require(gui::text_graphemes(family)) ==
               std::vector<std::size_t>{0, family.size()}));
    const std::string flags = "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8\xf0\x9f\x87\xa8";
    TFL_CHECK((tests::require(gui::text_graphemes(flags)) ==
               std::vector<std::size_t>{0, 8, 12}));
    const std::string conjunct = "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\x95";
    TFL_CHECK((tests::require(gui::text_graphemes(conjunct)) ==
               std::vector<std::size_t>{0, conjunct.size()}));
    for (const auto invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                               "\xe2\x82", "\x80"}) {
        const auto result = gui::text_graphemes(invalid);
        TFL_CHECK(!result.ok());
        TFL_CHECK(result.status().code == base::StatusCode::invalid_argument);
    }
    return 0;
}

int test_fallback_lifetimes_and_geometry() {
    auto context = tests::make_null_context();
    auto engine = tests::require(gui::TextEngine::create(context.device, {{256, 256}}));
    const auto primary = tests::require(engine.load_font(tests::gui_test_font(), 7));
    const auto fallback = tests::require(engine.load_font(tests::gui_unicode_font(), 7));
    const auto missing = tests::require(engine.shape(primary, "\xd7\x90"));
    TFL_CHECK(missing.glyphs.size() == 1 && missing.glyphs[0].id == 0);
    TFL_CHECK(engine.set_fallbacks(primary, std::array{fallback}).ok());
    TFL_CHECK(!engine.set_fallbacks(primary, std::array{fallback, fallback}).ok());
    TFL_CHECK(!engine.set_fallbacks(primary, std::array{primary}).ok());
    TFL_CHECK(!engine.set_fallbacks(primary, std::array{gui::FontId{}}).ok());
    const std::string mixed = "A \xd7\x90\xd7\x91 12 B";
    auto layout = tests::require(engine.shape(primary, mixed));
    TFL_CHECK(layout.glyphs.size() == 9);
    for (const auto& glyph : layout.glyphs) {
        TFL_CHECK(glyph.font == (glyph.cluster == 0 || glyph.cluster == 1 ||
                                glyph.cluster == 6 || glyph.cluster >= 9
                                    ? primary : fallback));
    }
    const auto position = [&](std::size_t byte) {
        const auto found = std::find_if(layout.clusters.begin(), layout.clusters.end(),
                                       [&](const auto& c) { return c.begin == byte; });
        return found == layout.clusters.end() ? -1.0F : found->bounds.x;
    };
    TFL_CHECK(position(4) < position(2)); // Hebrew reverses; decimal digits do not.
    TFL_CHECK(position(7) < position(8));
    TFL_CHECK(layout.caret_position(0).x == 0);
    TFL_CHECK(layout.hit_test({-100, -100}) == 0);
    const auto selected = layout.selection_rects(2, 6);
    TFL_CHECK(selected.size() == 1);
    TFL_CHECK(selected[0].width == 10);
    TFL_CHECK(layout.selection_rects(0, mixed.size()).size() >= 3);
    TFL_CHECK(layout.selection_rects(2, 2).empty());
    TFL_CHECK(layout.selection_rects(6, 2) == selected);
    TFL_CHECK(std::is_sorted(layout.carets.begin(), layout.carets.end(),
                             [](const auto& a, const auto& b) {
                                 return a.position.y != b.position.y
                                     ? a.position.y < b.position.y : a.position.x < b.position.x;
                             }));
    static_cast<void>(tests::require(engine.shape(primary, mixed)));
    TFL_CHECK(engine.last_stats().rasterized_glyphs == 0);
    TFL_CHECK(engine.last_stats().atlas_updates == 0);
    TFL_CHECK(engine.unload_font(fallback).code == base::StatusCode::invalid_state);
    TFL_CHECK(engine.set_fallbacks(primary, {}).ok());
    TFL_CHECK(engine.unload_font(fallback).ok());
    TFL_CHECK(!engine.set_fallbacks(primary, std::array{fallback}).ok());
    const auto replacement = tests::require(engine.load_font(tests::gui_unicode_font(), 7));
    TFL_CHECK(replacement != fallback);
    TFL_CHECK(engine.set_fallbacks(primary, std::array{replacement}).ok());
    auto replaced = tests::require(engine.shape(primary, "\xd7\x90"));
    TFL_CHECK(replaced.glyphs[0].font == replacement);
    TFL_CHECK(engine.last_stats().rasterized_glyphs == 1);
    TFL_CHECK(engine.shape(primary, "\xc0\xaf").status().code ==
               base::StatusCode::invalid_argument);
    return 0;
}

int test_multiline_combining_and_bidi_controls() {
    auto context = tests::make_null_context();
    auto engine = tests::require(gui::TextEngine::create(context.device, {{256, 256}}));
    const auto font = tests::require(engine.load_font(tests::gui_unicode_font(), 7));
    const auto lines = tests::require(engine.shape(font, "A\r\n\xd7\x90\xd7\x91\n\nB\n"));
    TFL_CHECK(lines.size.height == lines.line_height * 5);
    TFL_CHECK(lines.glyphs.size() == 4);
    TFL_CHECK(lines.caret_position(3).y == lines.line_height);
    TFL_CHECK(lines.caret_position(8).y == lines.line_height * 2);
    TFL_CHECK(lines.caret_position(11).y == lines.line_height * 4);
    TFL_CHECK(lines.selection_rects(0, 11).size() == 3);
    const auto separators = tests::require(engine.shape(font, "A\xe2\x80\xa8"
                                                               "B\xe2\x80\xa9"
                                                               "A\xc2\x85" "B"));
    TFL_CHECK(separators.size.height == separators.line_height * 4);
    TFL_CHECK(separators.glyphs.size() == 4);
    TFL_CHECK(separators.caret_position(4).y == separators.line_height);
    TFL_CHECK(separators.caret_position(8).y == separators.line_height * 2);
    TFL_CHECK(separators.caret_position(11).y == separators.line_height * 3);
    const auto empty = tests::require(engine.shape(font, ""));
    TFL_CHECK(empty.carets.size() == 1 && empty.carets[0].byte == 0);
    const auto home = tests::require(engine.shape(font, "AB\nBA"));
    TFL_CHECK(home.hit_test({-1e9F, home.line_height}) == 3);
    TFL_CHECK(home.hit_test({1e9F, home.line_height}) == 5);
    TFL_CHECK(home.hit_test({-1e9F, 0}) == 0);
    TFL_CHECK(home.hit_test({1e9F, 0}) == 2);
    const auto combining = tests::require(engine.shape(font, "e\xcc\x81"));
    TFL_CHECK(combining.clusters.size() == 1);
    TFL_CHECK(combining.clusters[0].begin == 0 && combining.clusters[0].end == 3);
    TFL_CHECK(combining.carets.size() == 2);
    TFL_CHECK(combining.selection_rects(1, 2).size() == 1);
    for (const std::string sequence : {
             "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8",
             "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
             "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",
             "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\x95",
             "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8"}) {
        const auto shaped = tests::require(engine.shape(font, sequence));
        TFL_CHECK(shaped.clusters.size() == 1);
        TFL_CHECK(shaped.clusters[0].begin == 0 && shaped.clusters[0].end == sequence.size());
        TFL_CHECK(shaped.carets.size() == 2);
        TFL_CHECK(shaped.carets[0].byte == 0);
        TFL_CHECK(shaped.carets[1].byte == sequence.size());
    }
    const auto isolated = tests::require(engine.shape(font, "A \xe2\x81\xa7\xd7\x90\xd7\x91"
                                                           "\xe2\x81\xa9 B"));
    const auto alef = std::find_if(isolated.glyphs.begin(), isolated.glyphs.end(),
                                  [](const auto& glyph) { return glyph.cluster == 5; });
    const auto bet = std::find_if(isolated.glyphs.begin(), isolated.glyphs.end(),
                                 [](const auto& glyph) { return glyph.cluster == 7; });
    TFL_CHECK(alef != isolated.glyphs.end() && bet != isolated.glyphs.end());
    TFL_CHECK(bet->position.x < alef->position.x);
    TFL_CHECK(isolated.glyphs.size() == 6);
    // Arabic and Latin share a paragraph but must use distinct shaping scripts.
    const auto arabic = tests::require(engine.shape(font, "A \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 12"));
    TFL_CHECK(!arabic.glyphs.empty());
    TFL_CHECK(std::all_of(arabic.glyphs.begin(), arabic.glyphs.end(),
                          [](const auto& glyph) { return glyph.id != 0; }));
    return 0;
}

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
    if (const int result = test_generated_ligature_and_script_shaping(); result != 0) {
        return result;
    }
    if (const int result = tests::gui_unicode_conformance(); result != 0) {
        return result;
    }
    if (const int result = test_unicode_boundaries(); result != 0) {
        return result;
    }
    if (const int result = test_fallback_lifetimes_and_geometry(); result != 0) {
        return result;
    }
    if (const int result = test_multiline_combining_and_bidi_controls(); result != 0) {
        return result;
    }
    if (const int result = test_moved_from_engine(); result != 0) {
        return result;
    }
    return test_shape_atlas_and_invalidation();
#else
    return 0;
#endif
}

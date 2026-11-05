#pragma once

#include "truffle/base/handle.hpp"
#include "truffle/base/status.hpp"
#include "truffle/gui/shapes.hpp"
#include "truffle/renderer/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace truffle::gui {

struct FontTag {};
using FontId = base::Handle<FontTag>;

struct TextGlyph {
    FontId font;
    std::uint32_t id = 0;
    std::uint32_t cluster = 0;
    Point position;
    Size size;
    Rect uv;
    float advance = 0.0F;

    friend bool operator==(const TextGlyph&, const TextGlyph&) = default;
};

struct TextCaret {
    std::size_t byte = 0;
    Point position;
    float height = 0.0F;

    friend bool operator==(const TextCaret&, const TextCaret&) = default;
};

// Logical grapheme coverage in visual coordinates, including ligature subdivisions.
struct TextCluster {
    std::size_t begin = 0;
    std::size_t end = 0;
    Rect bounds;
    bool right_to_left = false;

    friend bool operator==(const TextCluster&, const TextCluster&) = default;
};

// Unicode 16 extended grapheme boundaries (UAX #29), including 0 and utf8.size().
// Malformed UTF-8 is invalid_argument; text-disabled builds return unsupported.
[[nodiscard]] base::Result<std::vector<std::size_t>> text_graphemes(std::string_view utf8);

struct TextLayout {
    Size size;
    float ascent = 0.0F;
    float descent = 0.0F;
    float line_height = 0.0F;
    std::vector<TextGlyph> glyphs;
    // Line top y, then increasing x. Bidi junctions may have multiple logical
    // boundaries at one position, or one boundary at multiple positions.
    std::vector<TextCaret> carets;
    std::vector<TextCluster> clusters;

    // Picks the nearest line first, then its nearest source-byte caret.
    [[nodiscard]] std::size_t hit_test(Point point) const noexcept;
    // First visual match, or nearest source-byte boundary when not exact.
    [[nodiscard]] Point caret_position(std::size_t byte) const noexcept;
    // Half-open source selection; partial graphemes expand to their whole
    // cluster. Splits directional spans and lines; hard breaks have no area.
    [[nodiscard]] std::vector<Rect> selection_rects(std::size_t begin,
                                                   std::size_t end) const;

    friend bool operator==(const TextLayout&, const TextLayout&) = default;
};

struct TextBlock {
    TextLayout layout;
    TextureId atlas;
    Color color{1.0F, 1.0F, 1.0F, 1.0F};

    friend bool operator==(const TextBlock&, const TextBlock&) = default;
};

struct TextEngineDesc {
    rhi::Extent2D atlas_extent{1024, 1024};
};

struct TextStats {
    std::size_t shaped_glyphs = 0;
    std::size_t rasterized_glyphs = 0;
    std::size_t atlas_updates = 0;
};

// Shapes UTF-8 text with HarfBuzz and rasterizes its glyphs with FreeType.
// The engine owns an RGBA glyph atlas and updates only newly allocated glyph
// rectangles. It is single-thread-owned; the device must outlive the engine.
class TextEngine {
public:
    TextEngine();
    ~TextEngine();
    TextEngine(TextEngine&&) noexcept;
    TextEngine& operator=(TextEngine&&) noexcept;
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;

    [[nodiscard]] static bool available() noexcept;
    [[nodiscard]] static base::Result<TextEngine> create(
        rhi::Device& device, const TextEngineDesc& desc = {});

    // Copies the font bytes. Size is in logical GUI units; the renderer applies
    // the Context scale when the resulting shapes are drawn.
    [[nodiscard]] base::Result<FontId> load_font(std::span<const std::byte> bytes,
                                                 float size);
    [[nodiscard]] base::Status unload_font(FontId font);
    // Replaces the primary's flat ordered fallback chain atomically. All handles
    // must be live, distinct and different from primary. Unload of a referenced
    // fallback is invalid_state until its referencing chains are replaced.
    [[nodiscard]] base::Status set_fallbacks(FontId primary,
                                            std::span<const FontId> ordered);
    // Validated UTF-8, automatic UAX #9 bidi, script runs and hard newlines.
    // Uncovered graphemes use the primary font's explicit missing glyph (id 0);
    // malformed UTF-8 and invalid font handles fail instead of being replaced.
    [[nodiscard]] base::Result<TextLayout> shape(FontId font, std::string_view utf8);

    // The glyph atlas to register with a Painter. Fails with `invalid_state`
    // for a default-constructed or moved-from engine.
    [[nodiscard]] base::Result<std::reference_wrapper<renderer::SampledTexture>>
    atlas_texture() noexcept;
    [[nodiscard]] const TextStats& last_stats() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace truffle::gui

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
    std::uint32_t id = 0;
    std::uint32_t cluster = 0;
    Point position;
    Size size;
    Rect uv;
    float advance = 0.0F;

    friend bool operator==(const TextGlyph&, const TextGlyph&) = default;
};

struct TextLayout {
    Size size;
    float ascent = 0.0F;
    float descent = 0.0F;
    float line_height = 0.0F;
    std::vector<TextGlyph> glyphs;

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

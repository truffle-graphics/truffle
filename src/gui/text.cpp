#include "truffle/gui/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#if TRUFFLE_GUI_HAS_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#endif

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status failure(base::StatusCode code, std::string message) {
    return base::Status::failure(code, std::move(message));
}

#if TRUFFLE_GUI_HAS_TEXT
[[nodiscard]] std::uint32_t font_index(FontId font) noexcept {
    return static_cast<std::uint32_t>(font.value() & 0xFFFF'FFFFU);
}

[[nodiscard]] std::uint32_t font_generation(FontId font) noexcept {
    return static_cast<std::uint32_t>(font.value() >> 32U);
}

[[nodiscard]] FontId make_font_id(std::uint32_t index, std::uint32_t generation) noexcept {
    return FontId{(static_cast<std::uint64_t>(generation) << 32U) | index};
}

struct GlyphKey {
    std::uint64_t font = 0;
    std::uint32_t glyph = 0;

    friend bool operator==(const GlyphKey&, const GlyphKey&) = default;
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& key) const noexcept {
        return std::hash<std::uint64_t>{}(key.font) ^
               (std::hash<std::uint32_t>{}(key.glyph) << 1U);
    }
};

struct GlyphInfo {
    Point bearing;
    Size size;
    Rect uv;
};

struct FontData {
    std::vector<std::byte> bytes;
    FT_Face face = nullptr;
    hb_font_t* harfbuzz = nullptr;
    float size = 0.0F;

    ~FontData() {
        if (harfbuzz != nullptr) {
            hb_font_destroy(harfbuzz);
        }
        if (face != nullptr) {
            FT_Done_Face(face);
        }
    }
};

struct FontSlot {
    std::uint32_t generation = 1;
    std::unique_ptr<FontData> font;
};
#endif

} // namespace

struct TextEngine::State {
#if TRUFFLE_GUI_HAS_TEXT
    struct Atlas {
        rhi::Device* device = nullptr;
        renderer::SampledTexture texture;
        rhi::Extent2D extent{};
        std::vector<std::byte> pixels;
        std::unordered_map<GlyphKey, GlyphInfo, GlyphKeyHash> glyphs;
        std::uint32_t cursor_x = 1;
        std::uint32_t cursor_y = 1;
        std::uint32_t row_height = 0;

        [[nodiscard]] base::Result<GlyphInfo> rasterize(
            FontId font_id, FontData& font, std::uint32_t glyph_id, TextStats& stats) {
            const GlyphKey key{font_id.value(), glyph_id};
            if (const auto found = glyphs.find(key); found != glyphs.end()) {
                return found->second;
            }
            if (FT_Load_Glyph(font.face, glyph_id, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL) != 0 ||
                FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
                return failure(base::StatusCode::backend_error,
                               "FreeType could not rasterize a shaped glyph");
            }

            const FT_Bitmap& bitmap = font.face->glyph->bitmap;
            const std::uint32_t width = bitmap.width;
            const std::uint32_t height = bitmap.rows;
            GlyphInfo info{
                .bearing = {static_cast<float>(font.face->glyph->bitmap_left),
                            static_cast<float>(font.face->glyph->bitmap_top)},
                .size = {static_cast<float>(width), static_cast<float>(height)},
            };
            if (width == 0 || height == 0) {
                glyphs.emplace(key, info);
                return info;
            }
            if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY &&
                bitmap.pixel_mode != FT_PIXEL_MODE_MONO) {
                return failure(base::StatusCode::unsupported,
                               "FreeType returned an unsupported glyph bitmap format");
            }

            const std::uint32_t packed_width = width + 2;
            const std::uint32_t packed_height = height + 2;
            if (packed_width > extent.width || packed_height > extent.height) {
                return failure(base::StatusCode::out_of_memory,
                               "a glyph is larger than the configured GUI atlas");
            }
            if (cursor_x + packed_width > extent.width) {
                cursor_x = 1;
                cursor_y += row_height;
                row_height = 0;
            }
            if (cursor_y + packed_height > extent.height) {
                return failure(base::StatusCode::out_of_memory,
                               "the GUI glyph atlas is full");
            }
            const std::uint32_t packed_x = cursor_x;
            const std::uint32_t packed_y = cursor_y;
            const std::uint32_t x = packed_x + 1;
            const std::uint32_t y = packed_y + 1;
            info.uv = {static_cast<float>(x) / static_cast<float>(extent.width),
                       static_cast<float>(y) / static_cast<float>(extent.height),
                       static_cast<float>(width) / static_cast<float>(extent.width),
                       static_cast<float>(height) / static_cast<float>(extent.height)};

            std::vector<std::byte> region(static_cast<std::size_t>(packed_width) *
                                              packed_height * 4,
                                          std::byte{0});
            for (std::uint32_t row = 0; row < height; ++row) {
                const int pitch = bitmap.pitch;
                const unsigned char* source =
                    pitch >= 0 ? bitmap.buffer + static_cast<std::ptrdiff_t>(row) * pitch
                               : bitmap.buffer +
                                     static_cast<std::ptrdiff_t>(height - row - 1) * -pitch;
                for (std::uint32_t column = 0; column < width; ++column) {
                    std::uint8_t coverage = 0;
                    if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                        const std::uint32_t levels = std::max<std::uint32_t>(bitmap.num_grays, 2);
                        coverage = static_cast<std::uint8_t>(
                            static_cast<std::uint32_t>(source[column]) * 255U / (levels - 1U));
                    } else {
                        coverage = (source[column / 8U] & (0x80U >> (column % 8U))) != 0
                                       ? 255
                                       : 0;
                    }
                    const std::size_t pixel =
                        (static_cast<std::size_t>(row + 1) * packed_width + column + 1) * 4;
                    region[pixel] = std::byte{255};
                    region[pixel + 1] = std::byte{255};
                    region[pixel + 2] = std::byte{255};
                    region[pixel + 3] = static_cast<std::byte>(coverage);
                }
            }
            const std::size_t row_bytes = static_cast<std::size_t>(packed_width) * 4;
            for (std::uint32_t row = 0; row < packed_height; ++row) {
                const std::size_t destination =
                    (static_cast<std::size_t>(packed_y + row) * extent.width + packed_x) * 4;
                std::memcpy(pixels.data() + destination,
                            region.data() + static_cast<std::size_t>(row) * row_bytes,
                            row_bytes);
            }
            if (auto status = texture.update(*device, {packed_x, packed_y, 0},
                                             {packed_width, packed_height}, region, row_bytes);
                !status.ok()) {
                return status;
            }
            cursor_x += packed_width;
            row_height = std::max(row_height, packed_height);
            ++stats.atlas_updates;
            ++stats.rasterized_glyphs;
            glyphs.emplace(key, info);
            return info;
        }
    };

    FT_Library library = nullptr;
    std::vector<FontSlot> fonts;
    std::vector<std::uint32_t> free_fonts;
    Atlas atlas;
    TextStats stats;

    ~State() {
        fonts.clear();
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    [[nodiscard]] FontData* find(FontId id) noexcept {
        if (!id.valid()) {
            return nullptr;
        }
        const std::uint32_t index = font_index(id);
        if (index >= fonts.size()) {
            return nullptr;
        }
        FontSlot& slot = fonts[index];
        return slot.generation == font_generation(id) ? slot.font.get() : nullptr;
    }
#endif
};

TextEngine::TextEngine() = default;
TextEngine::~TextEngine() = default;
TextEngine::TextEngine(TextEngine&&) noexcept = default;
TextEngine& TextEngine::operator=(TextEngine&&) noexcept = default;

bool TextEngine::available() noexcept {
#if TRUFFLE_GUI_HAS_TEXT
    return true;
#else
    return false;
#endif
}

base::Result<TextEngine> TextEngine::create(rhi::Device& device, const TextEngineDesc& desc) {
#if TRUFFLE_GUI_HAS_TEXT
    if (desc.atlas_extent.width < 8 || desc.atlas_extent.height < 8 ||
        desc.atlas_extent.width > 4096 || desc.atlas_extent.height > 4096) {
        return failure(base::StatusCode::invalid_argument,
                       "GUI text atlas extent must be between 8 and 4096 pixels");
    }
    auto state = std::make_unique<State>();
    if (FT_Init_FreeType(&state->library) != 0) {
        return failure(base::StatusCode::unavailable, "FreeType initialization failed");
    }
    state->atlas.device = &device;
    state->atlas.extent = desc.atlas_extent;
    const std::size_t pixel_count =
        static_cast<std::size_t>(desc.atlas_extent.width) * desc.atlas_extent.height;
    state->atlas.pixels.resize(pixel_count * 4, std::byte{0});
    auto texture = renderer::SampledTexture::upload(
        device, {.extent = desc.atlas_extent,
                 .format = rhi::TextureFormat::rgba8_unorm,
                 .bytes = state->atlas.pixels,
                 .debugName = "Truffle GUI glyph atlas"});
    if (!texture.ok()) {
        return texture.status();
    }
    state->atlas.texture = std::move(texture).value();
    TextEngine engine;
    engine.state_ = std::move(state);
    return engine;
#else
    static_cast<void>(device);
    static_cast<void>(desc);
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TRUFFLE_GUI_ENABLE_TEXT=ON");
#endif
}

base::Result<FontId> TextEngine::load_font(std::span<const std::byte> bytes, float size) {
#if TRUFFLE_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    if (bytes.empty() || !std::isfinite(size) || size <= 0.0F || size > 512.0F ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) {
        return failure(base::StatusCode::invalid_argument,
                       "font data must be non-empty and font size finite in (0, 512]");
    }
    auto font = std::make_unique<FontData>();
    font->bytes.assign(bytes.begin(), bytes.end());
    if (FT_New_Memory_Face(state_->library,
                           reinterpret_cast<const FT_Byte*>(font->bytes.data()),
                           static_cast<FT_Long>(font->bytes.size()), 0, &font->face) != 0 ||
        FT_Set_Char_Size(font->face, 0, static_cast<FT_F26Dot6>(std::lround(size * 64.0F)),
                        72, 72) != 0) {
        return failure(base::StatusCode::invalid_argument,
                       "FreeType could not load the font or select its size");
    }
    font->harfbuzz = hb_ft_font_create_referenced(font->face);
    if (font->harfbuzz == nullptr) {
        return failure(base::StatusCode::unavailable, "HarfBuzz font creation failed");
    }
    hb_ft_font_changed(font->harfbuzz);
    font->size = size;

    std::uint32_t index = 0;
    if (state_->free_fonts.empty()) {
        index = static_cast<std::uint32_t>(state_->fonts.size());
        state_->fonts.emplace_back();
    } else {
        index = state_->free_fonts.back();
        state_->free_fonts.pop_back();
    }
    state_->fonts[index].font = std::move(font);
    return make_font_id(index, state_->fonts[index].generation);
#else
    static_cast<void>(bytes);
    static_cast<void>(size);
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TRUFFLE_GUI_ENABLE_TEXT=ON");
#endif
}

base::Status TextEngine::unload_font(FontId font) {
#if TRUFFLE_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    auto* entry = state_->find(font);
    if (entry == nullptr) {
        return failure(base::StatusCode::invalid_argument, "GUI font handle is not alive");
    }
    const std::uint32_t index = font_index(font);
    state_->fonts[index].font.reset();
    if (state_->fonts[index].generation != std::numeric_limits<std::uint32_t>::max()) {
        ++state_->fonts[index].generation;
        state_->free_fonts.push_back(index);
    }
    return base::Status::success();
#else
    static_cast<void>(font);
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TRUFFLE_GUI_ENABLE_TEXT=ON");
#endif
}

base::Result<TextLayout> TextEngine::shape(FontId font, std::string_view utf8) {
#if TRUFFLE_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return failure(base::StatusCode::invalid_argument, "text input is too large to shape");
    }
    FontData* face = state_->find(font);
    if (face == nullptr) {
        return failure(base::StatusCode::invalid_argument, "GUI font handle is not alive");
    }
    state_->stats = {};
    hb_buffer_t* buffer = hb_buffer_create();
    if (buffer == nullptr) {
        return failure(base::StatusCode::out_of_memory, "HarfBuzz buffer allocation failed");
    }
    hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()), 0,
                       static_cast<int>(utf8.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(face->harfbuzz, buffer, nullptr, 0);
    unsigned int count = 0;
    const hb_glyph_info_t* glyph_infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    TextLayout layout;
    const FT_Size_Metrics metrics = face->face->size->metrics;
    layout.ascent = static_cast<float>(metrics.ascender) / 64.0F;
    layout.descent = std::max(0.0F, -static_cast<float>(metrics.descender) / 64.0F);
    layout.line_height = std::max(static_cast<float>(metrics.height) / 64.0F, face->size);
    layout.glyphs.reserve(count);
    float pen_x = 0.0F;
    float pen_y = 0.0F;
    for (unsigned int i = 0; i < count; ++i) {
        auto glyph = state_->atlas.rasterize(font, *face, glyph_infos[i].codepoint,
                                             state_->stats);
        if (!glyph.ok()) {
            hb_buffer_destroy(buffer);
            return glyph.status();
        }
        const float x_offset = static_cast<float>(positions[i].x_offset) / 64.0F;
        const float y_offset = static_cast<float>(positions[i].y_offset) / 64.0F;
        const GlyphInfo& info = glyph.value();
        layout.glyphs.push_back({
            .id = glyph_infos[i].codepoint,
            .cluster = glyph_infos[i].cluster,
            .position = {pen_x + x_offset + info.bearing.x,
                         layout.ascent - pen_y - y_offset - info.bearing.y},
            .size = info.size,
            .uv = info.uv,
            .advance = static_cast<float>(positions[i].x_advance) / 64.0F,
        });
        pen_x += static_cast<float>(positions[i].x_advance) / 64.0F;
        pen_y += static_cast<float>(positions[i].y_advance) / 64.0F;
    }
    hb_buffer_destroy(buffer);
    layout.size = {std::max(0.0F, pen_x), layout.line_height};
    state_->stats.shaped_glyphs = count;
    return layout;
#else
    static_cast<void>(font);
    static_cast<void>(utf8);
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TRUFFLE_GUI_ENABLE_TEXT=ON");
#endif
}

renderer::SampledTexture& TextEngine::atlas_texture() noexcept {
#if TRUFFLE_GUI_HAS_TEXT
    return state_->atlas.texture;
#else
    static renderer::SampledTexture unavailable;
    return unavailable;
#endif
}

const TextStats& TextEngine::last_stats() const noexcept {
#if TRUFFLE_GUI_HAS_TEXT
    static const TextStats empty;
    return state_ ? state_->stats : empty;
#else
    static const TextStats empty;
    return empty;
#endif
}

} // namespace truffle::gui

#include "truffle/gui/text.hpp"
#include "text_unicode.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#if TFL_GUI_HAS_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>
#endif

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status failure(base::StatusCode code, std::string message) {
    return base::Status::failure(code, std::move(message));
}

#if TFL_GUI_HAS_TEXT
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
    std::vector<FontId> fallbacks;

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
#if TFL_GUI_HAS_TEXT
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
#if TFL_GUI_HAS_TEXT
    return true;
#else
    return false;
#endif
}

base::Result<TextEngine> TextEngine::create(rhi::Device& device, const TextEngineDesc& desc) {
#if TFL_GUI_HAS_TEXT
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
                   "TFL_GUI_ENABLE_TEXT=ON");
#endif
}

base::Result<FontId> TextEngine::load_font(std::span<const std::byte> bytes, float size) {
#if TFL_GUI_HAS_TEXT
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
                   "TFL_GUI_ENABLE_TEXT=ON");
#endif
}

base::Status TextEngine::unload_font(FontId font) {
#if TFL_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    auto* entry = state_->find(font);
    if (entry == nullptr) {
        return failure(base::StatusCode::invalid_argument, "GUI font handle is not alive");
    }
    for (const auto& slot : state_->fonts) {
        if (slot.font && std::find(slot.font->fallbacks.begin(), slot.font->fallbacks.end(), font) !=
                             slot.font->fallbacks.end()) {
            return failure(base::StatusCode::invalid_state,
                           "GUI font is referenced by a fallback chain");
        }
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
                   "TFL_GUI_ENABLE_TEXT=ON");
#endif
}

base::Status TextEngine::set_fallbacks(FontId primary, std::span<const FontId> ordered) {
#if TFL_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    auto* entry = state_->find(primary);
    if (entry == nullptr) {
        return failure(base::StatusCode::invalid_argument, "GUI primary font handle is not alive");
    }
    std::vector<FontId> chain;
    for (const auto font : ordered) {
        if (state_->find(font) == nullptr || font == primary ||
            std::find(chain.begin(), chain.end(), font) != chain.end()) {
            return failure(base::StatusCode::invalid_argument,
                           "GUI fallback fonts must be live, distinct and not the primary");
        }
        chain.push_back(font);
    }
    entry->fallbacks = std::move(chain);
    return base::Status::success();
#else
    static_cast<void>(primary);
    static_cast<void>(ordered);
    return failure(base::StatusCode::unsupported, "GUI Unicode text is disabled");
#endif
}

base::Result<TextLayout> TextEngine::shape(FontId font, std::string_view utf8) {
#if TFL_GUI_HAS_TEXT
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
    auto decoded = unicode::decode(utf8);
    if (!decoded.ok()) {
        return decoded.status();
    }
    const auto& input = decoded.value();
    auto visual = unicode::visual_lines(utf8, input);
    if (!visual.ok()) {
        return visual.status();
    }
    std::vector<FontId> chain{font};
    chain.insert(chain.end(), face->fallbacks.begin(), face->fallbacks.end());
    for (const auto candidate : chain) {
        if (state_->find(candidate) == nullptr) {
            return failure(base::StatusCode::invalid_argument, "GUI fallback font handle is stale");
        }
    }
    using Buffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;
    const auto shape_buffer = [&](FontId candidate, const unicode::Run& run,
                                  std::size_t begin, std::size_t end) {
        Buffer buffer{hb_buffer_create(), hb_buffer_destroy};
        hb_buffer_set_direction(buffer.get(), run.rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_script(buffer.get(), hb_script_from_iso15924_tag(run.script));
        hb_buffer_set_language(buffer.get(), hb_language_from_string("und", -1));
        hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_set_flags(buffer.get(), HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
        // Full source context preserves joining across font/script item boundaries.
        hb_buffer_add_utf8(buffer.get(), utf8.data(), static_cast<int>(utf8.size()),
                           static_cast<unsigned int>(begin), static_cast<int>(end - begin));
        hb_shape(state_->find(candidate)->harfbuzz, buffer.get(), nullptr, 0);
        return buffer;
    };
    struct Segment {
        unicode::Run run;
        FontId font;
    };
    std::vector<std::vector<Segment>> planned;
    TextLayout layout;
    const auto include_metrics = [&](FontId id) {
        const auto* data = state_->find(id);
        const auto metrics = data->face->size->metrics;
        layout.ascent = std::max(layout.ascent, static_cast<float>(metrics.ascender) / 64.0F);
        layout.descent = std::max(layout.descent, -static_cast<float>(metrics.descender) / 64.0F);
        layout.line_height = std::max({layout.line_height, data->size,
                                      static_cast<float>(metrics.height) / 64.0F});
    };
    include_metrics(font);
    for (const auto& line : visual.value()) {
        std::vector<Segment> segments;
        for (const auto& run : line.runs) {
            std::vector<Segment> logical;
            for (std::size_t begin = run.begin; begin < run.end;) {
                const auto next = std::upper_bound(input.graphemes.begin(), input.graphemes.end(), begin);
                const auto end = std::min(run.end, next == input.graphemes.end() ? run.end : *next);
                FontId selected;
                for (const auto candidate : chain) {
                    // Shape the complete grapheme to include canonical composition,
                    // variation sequences and font-specific substitutions in coverage.
                    auto probe = shape_buffer(candidate, run, begin, end);
                    if (!hb_buffer_allocation_successful(probe.get())) {
                        return failure(base::StatusCode::out_of_memory, "HarfBuzz allocation failed");
                    }
                    unsigned int count = 0;
                    const auto* infos = hb_buffer_get_glyph_infos(probe.get(), &count);
                    bool covered = true;
                    for (unsigned int i = 0; i < count; ++i) {
                        if (infos[i].codepoint != 0) {
                            continue;
                        }
                        const auto cp = std::lower_bound(input.codepoints.begin(), input.codepoints.end(),
                            infos[i].cluster, [](const auto& value, std::size_t byte) {
                                return value.begin < byte;
                            });
                        if (cp == input.codepoints.end() || !unicode::ignorable(cp->value)) {
                            covered = false;
                            break;
                        }
                    }
                    if (covered) {
                        selected = candidate;
                        break;
                    }
                }
                if (!selected.valid()) {
                    selected = font;
                }
                include_metrics(selected);
                if (!logical.empty() && logical.back().font == selected) {
                    logical.back().run.end = end;
                } else {
                    logical.push_back({{begin, end, run.script, run.rtl}, selected});
                }
                begin = end;
            }
            if (run.rtl) {
                std::reverse(logical.begin(), logical.end());
            }
            segments.insert(segments.end(), logical.begin(), logical.end());
        }
        planned.push_back(std::move(segments));
    }
    layout.line_height = std::max(layout.line_height, layout.ascent + layout.descent);
    float line_y = 0.0F;
    for (std::size_t line_index = 0; line_index < planned.size(); ++line_index) {
        float pen_x = 0.0F;
        for (const auto& segment : planned[line_index]) {
            const auto& run = segment.run;
            auto buffer = shape_buffer(segment.font, run, run.begin, run.end);
            if (!hb_buffer_allocation_successful(buffer.get())) {
                return failure(base::StatusCode::out_of_memory, "HarfBuzz allocation failed");
            }
            unsigned int count = 0;
            const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
            const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);
            struct Coverage {
                float left;
                float right;
                std::uint32_t glyph = 0;
                unsigned int glyph_count = 0;
                float origin = 0.0F;
            };
            std::map<std::size_t, Coverage> coverage;
            const float run_x = pen_x;
            for (unsigned int i = 0; i < count; ++i) {
                const auto cp = std::lower_bound(input.codepoints.begin(), input.codepoints.end(),
                    infos[i].cluster, [](const auto& value, std::size_t byte) {
                        return value.begin < byte;
                    });
                const bool hidden = infos[i].codepoint == 0 &&
                    cp != input.codepoints.end() && unicode::ignorable(cp->value);
                const float advance = static_cast<float>(positions[i].x_advance) / 64.0F;
                const auto grapheme_end = std::upper_bound(input.graphemes.begin(),
                                                            input.graphemes.end(), infos[i].cluster);
                const auto grapheme_begin = *std::prev(grapheme_end);
                auto [found, inserted] = coverage.try_emplace(grapheme_begin,
                                                               Coverage{pen_x, pen_x + advance});
                if (!inserted) {
                    found->second.left = std::min(found->second.left, pen_x);
                    found->second.right = std::max(found->second.right, pen_x + advance);
                }
                found->second.glyph = infos[i].codepoint;
                ++found->second.glyph_count;
                found->second.origin = pen_x + static_cast<float>(positions[i].x_offset) / 64.0F;
                if (!hidden) {
                    auto glyph = state_->atlas.rasterize(segment.font, *state_->find(segment.font),
                                                         infos[i].codepoint, state_->stats);
                    if (!glyph.ok()) {
                        return glyph.status();
                    }
                    const auto& info = glyph.value();
                    layout.glyphs.push_back({
                        .font = segment.font,
                        .id = infos[i].codepoint,
                        .cluster = infos[i].cluster,
                        .position = {pen_x + static_cast<float>(positions[i].x_offset) / 64.0F +
                                         info.bearing.x,
                                     line_y + layout.ascent -
                                         static_cast<float>(positions[i].y_offset) / 64.0F -
                                         info.bearing.y},
                        .size = info.size, .uv = info.uv, .advance = advance});
                }
                pen_x += advance;
            }
            if (coverage.empty() || coverage.begin()->first > run.begin) {
                const float edge = run.rtl ? pen_x : run_x;
                const auto next = std::upper_bound(input.graphemes.begin(), input.graphemes.end(),
                                                    run.begin);
                coverage.emplace(*std::prev(next), Coverage{edge, edge});
            }
            for (auto it = coverage.begin(); it != coverage.end(); ++it) {
                const auto next = std::next(it);
                const auto end = next == coverage.end() ? run.end : next->first;
                std::vector<std::size_t> boundaries;
                const auto first = std::lower_bound(input.graphemes.begin(), input.graphemes.end(),
                                                     it->first);
                auto last = std::lower_bound(input.graphemes.begin(), input.graphemes.end(), end);
                if (last != input.graphemes.end()) {
                    ++last;
                }
                boundaries.assign(first, last);
                if (boundaries.size() < 2) {
                    continue;
                }
                std::vector<bool> visible;
                std::size_t visible_count = 0;
                for (std::size_t j = 0; j + 1 < boundaries.size(); ++j) {
                    auto cp = std::lower_bound(input.codepoints.begin(), input.codepoints.end(),
                        boundaries[j], [](const auto& value, std::size_t byte) {
                            return value.begin < byte;
                        });
                    bool drawn = false;
                    for (; cp != input.codepoints.end() && cp->begin < boundaries[j + 1]; ++cp) {
                        drawn = drawn || !unicode::ignorable(cp->value);
                    }
                    visible.push_back(drawn);
                    visible_count += drawn ? 1 : 0;
                }
                std::vector<float> stops{it->second.left};
                if (visible_count > 1 && it->second.glyph_count == 1) {
                    auto* hb_font = state_->find(segment.font)->harfbuzz;
                    const auto direction = run.rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
                    unsigned int caret_count = 0;
                    const auto total = hb_ot_layout_get_ligature_carets(
                        hb_font, direction, it->second.glyph, 0, &caret_count, nullptr);
                    if (total == visible_count - 1) {
                        std::vector<hb_position_t> caret_positions(total);
                        caret_count = total;
                        hb_ot_layout_get_ligature_carets(hb_font, direction, it->second.glyph,
                                                         0, &caret_count, caret_positions.data());
                        for (const auto position : caret_positions) {
                            stops.push_back(it->second.origin + static_cast<float>(position) / 64.0F);
                        }
                        std::sort(stops.begin() + 1, stops.end());
                    }
                }
                stops.push_back(it->second.right);
                if (stops.size() != visible_count + 1 || !std::is_sorted(stops.begin(), stops.end())) {
                    // Fonts without usable GDEF carets use equal subdivisions.
                    stops.assign(visible_count + 1, it->second.left);
                    for (std::size_t j = 1; j <= visible_count; ++j) {
                        stops[j] += (it->second.right - it->second.left) *
                                    static_cast<float>(j) / static_cast<float>(visible_count);
                    }
                }
                std::size_t logical_index = 0;
                for (std::size_t j = 0; j + 1 < boundaries.size(); ++j) {
                    const auto visual_index = run.rtl
                        ? visible_count - logical_index - (visible[j] ? 1 : 0) : logical_index;
                    const float x = stops[visual_index];
                    const float width = visible[j] ? stops[visual_index + 1] - x : 0.0F;
                    layout.clusters.push_back({boundaries[j], boundaries[j + 1],
                                              {x, line_y, width, layout.line_height}, run.rtl});
                    logical_index += visible[j] ? 1 : 0;
                }
            }
        }
        const auto& line = visual.value()[line_index];
        if (planned[line_index].empty()) {
            layout.carets.push_back({line.begin, {0.0F, line_y}, layout.line_height});
        }
        layout.size.width = std::max(layout.size.width, pen_x);
        line_y += layout.line_height;
    }
    // UAX #9/script item boundaries can cut an extended grapheme (e.g. Prepend
    // followed by another script). Reunite its geometry without exposing an
    // illegal editing stop at the item boundary.
    std::map<std::tuple<float, std::size_t, std::size_t>, TextCluster> grapheme_geometry;
    for (const auto& cluster : layout.clusters) {
        auto [found, inserted] = grapheme_geometry.try_emplace(
            std::tuple{cluster.bounds.y, cluster.begin, cluster.end}, cluster);
        if (!inserted) {
            auto& previous = found->second;
            if (previous.bounds.width == 0 && cluster.bounds.width > 0) {
                previous.right_to_left = cluster.right_to_left;
            }
            const float right = std::max(previous.bounds.x + previous.bounds.width,
                                          cluster.bounds.x + cluster.bounds.width);
            previous.bounds.x = std::min(previous.bounds.x, cluster.bounds.x);
            previous.bounds.width = right - previous.bounds.x;
        }
    }
    layout.clusters.clear();
    for (const auto& [key, cluster] : grapheme_geometry) {
        static_cast<void>(key);
        layout.clusters.push_back(cluster);
    }
    std::stable_sort(layout.clusters.begin(), layout.clusters.end(), [](const auto& a, const auto& b) {
        return a.bounds.y != b.bounds.y ? a.bounds.y < b.bounds.y : a.bounds.x < b.bounds.x;
    });
    for (const auto& cluster : layout.clusters) {
        layout.carets.push_back({cluster.right_to_left ? cluster.end : cluster.begin,
                                {cluster.bounds.x, cluster.bounds.y}, cluster.bounds.height});
        layout.carets.push_back({cluster.right_to_left ? cluster.begin : cluster.end,
                                {cluster.bounds.x + cluster.bounds.width, cluster.bounds.y},
                                cluster.bounds.height});
    }
    std::stable_sort(layout.carets.begin(), layout.carets.end(), [](const auto& a, const auto& b) {
        return a.position.y != b.position.y ? a.position.y < b.position.y : a.position.x < b.position.x;
    });
    layout.carets.erase(std::unique(layout.carets.begin(), layout.carets.end()), layout.carets.end());
    layout.size.height = line_y;
    state_->stats.shaped_glyphs = layout.glyphs.size();
    return layout;
#else
    static_cast<void>(font);
    static_cast<void>(utf8);
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TFL_GUI_ENABLE_TEXT=ON");
#endif
}

base::Result<std::reference_wrapper<renderer::SampledTexture>>
TextEngine::atlas_texture() noexcept {
#if TFL_GUI_HAS_TEXT
    if (!state_) {
        return failure(base::StatusCode::invalid_state, "GUI text engine is not initialized");
    }
    return std::ref(state_->atlas.texture);
#else
    return failure(base::StatusCode::unsupported,
                   "Truffle GUI text is unavailable; configure with "
                   "TFL_GUI_ENABLE_TEXT=ON");
#endif
}

const TextStats& TextEngine::last_stats() const noexcept {
#if TFL_GUI_HAS_TEXT
    static const TextStats empty;
    return state_ ? state_->stats : empty;
#else
    static const TextStats empty;
    return empty;
#endif
}

} // namespace truffle::gui

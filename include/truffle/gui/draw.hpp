#pragma once

#include "truffle/gui/context.hpp"
#include "truffle/gui/shapes.hpp"
#include "truffle/rhi/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace truffle::gui {

struct DrawOptions {
    rhi::Extent2D target; // render target size in pixels
    float scale = 1.0F;   // pixels per layout unit
    bool clip_y_up = true; // clip-space y grows upwards (Metal); false flips it
};

// Indices drawn with one texture and one clip rectangle, in target pixels.
struct DrawCommand {
    TextureId texture;
    rhi::ScissorRect clip;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

// One frame of GUI geometry. Vertices are in clip space with `local` and
// `shape` in pixels; indices address the shared vertex array.
struct DrawData {
    std::vector<ShapeVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawCommand> commands; // in drawing order
    std::size_t elements = 0;          // elements whose shapes were drawn
    std::size_t clipped = 0;           // subtrees skipped as fully clipped
};

// Combines the stored shapes of `context` into `out`, reusing its capacity.
// Elements draw parent first, then children in order; every element is
// clipped by the bounds of ancestors that clip their children. Only adjacent
// runs with the same texture and clip merge, so visual order is preserved.
// Shapes are not regenerated here; call `Context::update_shapes` first.
void assemble(const Context& context, const DrawOptions& options, DrawData& out);

} // namespace truffle::gui

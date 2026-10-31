#pragma once

#include "truffle/base/status.hpp"
#include "truffle/gui/context.hpp"
#include "truffle/gui/draw.hpp"
#include "truffle/renderer/pipeline_cache.hpp"
#include "truffle/renderer/render_batch.hpp"
#include "truffle/renderer/texture.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace truffle::gui {

struct PainterDesc {
    // Program slot registered in the host's PipelineCache.
    renderer::MaterialId material = 0x7467'7569'0000'0001ULL;
    std::size_t variant_hash = 0;
    rhi::TextureFormat color_format = rhi::TextureFormat::rgba8_unorm;
    std::uint32_t frames_in_flight = 2;
};

struct PaintStats {
    std::size_t elements = 0; // elements whose shapes were drawn
    std::size_t batches = 0;
    std::size_t clipped = 0;  // subtrees skipped as fully clipped
};

// Turns a Context into Renderer batches for a host-supplied color target.
// Programs come from a prepared shader package, so no shader compiler runs.
// The host owns the device, pacing and render pass: it calls `record` with
// the FramePacer slot and adds the batches to its own RenderPassNode, whose
// color attachment must use `color_format` and match `target`.
// Currently Metal only. Single-thread-owned; the device and cache outlive it.
class Painter {
public:
    Painter();
    ~Painter();
    Painter(Painter&&) noexcept;
    Painter& operator=(Painter&&) noexcept;
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    // Registers the GUI program in `programs` under `desc.material`.
    [[nodiscard]] static base::Result<Painter> create(rhi::Device& device,
                                                      renderer::PipelineCache& programs,
                                                      const PainterDesc& desc = {});

    // Makes `texture` available to decorations. The texture is borrowed: keep
    // it alive and unmoved until it is removed or the painter is destroyed.
    [[nodiscard]] base::Result<TextureId> add_texture(renderer::SampledTexture& texture,
                                                      const rhi::SamplerDesc& sampler = {});
    [[nodiscard]] base::Status remove_texture(TextureId texture);

    // Assembles the stored shapes of `context` and streams them into frame
    // `slot` (from FramePacer::begin_frame). Batches stay valid until the slot
    // begins again. Logical units map to `target` pixels through the
    // context's scale. Fails if a shape uses an unregistered texture.
    [[nodiscard]] base::Result<std::vector<renderer::RenderBatch>> record(
        const Context& context, std::uint32_t slot, rhi::Extent2D target);

    [[nodiscard]] const PaintStats& last_stats() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace truffle::gui

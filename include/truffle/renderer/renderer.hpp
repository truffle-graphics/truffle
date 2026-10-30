#pragma once

#include "truffle/base/status.hpp"
#include "truffle/renderer/pipeline_cache.hpp"
#include "truffle/renderer/render_batch.hpp"
#include "truffle/renderer/frame_graph.hpp"
#include "truffle/renderer/transform_compute_pass.hpp"
#include "truffle/rhi/rhi.hpp"

#include <memory>
#include <span>

namespace truffle::renderer {

struct RendererFrameStats {
    std::uint32_t computeNodesExecuted = 0;
    std::uint32_t renderNodesExecuted = 0;
    std::uint32_t renderBatchesExecuted = 0;
    std::uint32_t renderBatchesClipped = 0; // skipped: empty clip intersection
    bool submitted = false; // graphics work reached the queue (signal included)
    bool presented = false;
};

// Optional completion signal for the frame's single graphics submission. The
// fence reaches `value` when the GPU has finished the recorded work, including
// when presentation itself later fails. Nothing is signaled if submission did
// not happen (see RendererFrameStats::submitted).
struct RenderSignal {
    rhi::Fence* fence = nullptr;
    std::uint64_t value = 0;
};

class Renderer {
public:
    explicit Renderer(rhi::Device& device, IPipelineCache* cache = nullptr);

    // The device/cache outlive Renderer. Recording and cache mutation belong to
    // one host thread; do not stage/commit concurrently with render().
    // The default cache is shaderless and intended only for Null validation.
    // Render a full frame graph. Pass a swapchain for presentation-capable paths;
    // omit (or pass nullptr) for headless and null-backend testing.
    [[nodiscard]] base::Status render(const FrameGraph& graph,
                                      rhi::Swapchain* swapchain = nullptr,
                                      RenderSignal signal = {});

    [[nodiscard]] const RendererFrameStats& last_frame_stats() const noexcept {
        return lastFrameStats_;
    }

private:
    rhi::Device*        device_ = nullptr;
    IPipelineCache*     cache_  = nullptr;
    std::unique_ptr<NullPipelineCache> defaultCache_;
    RendererFrameStats  lastFrameStats_{};
};

} // namespace truffle::renderer

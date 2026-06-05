#pragma once

#include "truffle/core/status.hpp"
#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/render/frame_graph.hpp"
#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/rhi.hpp"

#include <span>

namespace truffle::render {

struct RendererFrameStats {
    std::uint32_t computeNodesExecuted = 0;
    std::uint32_t renderNodesExecuted = 0;
    std::uint32_t renderBatchesExecuted = 0;
    bool presented = false;
};

class Renderer {
public:
    explicit Renderer(rhi::IDevice& device, IPipelineCache* cache = nullptr);

    // Render batches. Pass a swapchain for presentation-capable paths;
    // omit (or pass nullptr) for headless and null-backend testing.
    // Render a full frame graph. Pass a swapchain for presentation-capable paths;
    // omit (or pass nullptr) for headless and null-backend testing.
    [[nodiscard]] core::Status render(const FrameGraph& graph,
                                      rhi::ISwapchain* swapchain = nullptr);

    [[nodiscard]] const RendererFrameStats& last_frame_stats() const noexcept {
        return lastFrameStats_;
    }

private:
    rhi::IDevice*       device_ = nullptr;
    IPipelineCache*     cache_  = nullptr;
    RendererFrameStats  lastFrameStats_{};
};

} // namespace truffle::render


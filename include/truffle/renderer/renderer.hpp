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
    bool presented = false;
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
                                      rhi::Swapchain* swapchain = nullptr);

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

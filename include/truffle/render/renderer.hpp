#pragma once

#include "truffle/core/status.hpp"
#include "truffle/render/pipeline_cache.hpp"
#include "truffle/render/render_batch.hpp"
#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/rhi.hpp"

#include <span>

namespace truffle::render {

class Renderer {
public:
    explicit Renderer(rhi::IDevice& device, IPipelineCache* cache = nullptr);

    // Render batches. Pass a swapchain for presentation-capable paths;
    // omit (or pass nullptr) for headless and null-backend testing.
    [[nodiscard]] core::Status render(std::span<const RenderBatch> batches,
                                      rhi::ISwapchain* swapchain = nullptr,
                                      TransformComputePass* computePass = nullptr,
                                      const TransformComputePassDesc* computeDesc = nullptr);

private:
    rhi::IDevice*   device_ = nullptr;
    IPipelineCache* cache_  = nullptr;
};

} // namespace truffle::render


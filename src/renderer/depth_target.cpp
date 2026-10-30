#include "truffle/renderer/depth_target.hpp"

#include <array>

namespace truffle::renderer {

base::Status DepthTarget::resize(rhi::Extent2D extent, rhi::TextureFormat format,
                                 std::uint32_t samples) {
    const bool depthFormat = format == rhi::TextureFormat::depth16_unorm ||
        format == rhi::TextureFormat::depth24_unorm_stencil8 ||
        format == rhi::TextureFormat::depth32_float ||
        format == rhi::TextureFormat::depth32_float_stencil8;
    if (extent.width == 0 || extent.height == 0 || !depthFormat) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "Renderer: depth target requires nonzero extent and depth format");
    }
    if (texture_.valid()) {
        const auto current = texture_.desc();
        if (current.extent.width == extent.width && current.extent.height == extent.height &&
            current.format == format && current.sampleCount == samples) {
            return base::Status::success();
        }
    }
    auto result = device_->create_texture({
        .extent = {extent.width, extent.height, 1}, .format = format,
        .usage = rhi::TextureUsage::depth_stencil_attachment, .sampleCount = samples,
        .debugName = "Renderer depth target",
    });
    if (!result.ok()) {
        return result.status();
    }
    auto next = std::move(result).value();
    auto poolResult = device_->create_command_pool(rhi::QueueKind::graphics);
    if (!poolResult.ok()) {
        return poolResult.status();
    }
    auto pool = std::move(poolResult).value();
    auto listResult = pool.allocate();
    if (!listResult.ok()) {
        return listResult.status();
    }
    auto list = std::move(listResult).value();
    if (auto status = list.begin(); !status.ok()) {
        return status;
    }
    rhi::BarrierBatch barriers;
    const bool stencil = format == rhi::TextureFormat::depth24_unorm_stencil8 ||
                          format == rhi::TextureFormat::depth32_float_stencil8;
    barriers.textures.push_back({
        .texture = &next,
        .range = {.aspects = stencil ? rhi::TextureAspect::depth | rhi::TextureAspect::stencil
                                     : rhi::TextureAspect::depth},
        .oldLayout = rhi::TextureLayout::undefined,
        .newLayout = rhi::TextureLayout::depth_stencil_attachment,
        .sourceStages = rhi::PipelineStage::top,
        .destinationStages = rhi::PipelineStage::early_fragment_tests |
                             rhi::PipelineStage::late_fragment_tests,
        .destinationAccess = rhi::Access::depth_stencil_read | rhi::Access::depth_stencil_write,
    });
    if (auto status = list.barrier(barriers); !status.ok()) {
        return status;
    }
    if (auto status = list.end(); !status.ok()) {
        return status;
    }
    auto queue = device_->queue(rhi::QueueKind::graphics);
    if (!queue.ok()) {
        return queue.status();
    }
    const std::array<rhi::CommandList*, 1> lists{&list};
    if (auto status = queue.value().submit(lists); !status.ok()) {
        return status;
    }
    texture_ = std::move(next);
    return base::Status::success();
}

} // namespace truffle::renderer

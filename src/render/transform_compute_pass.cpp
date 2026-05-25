#include "truffle/render/transform_compute_pass.hpp"

namespace truffle::render {

TransformComputePass::TransformComputePass(rhi::IDevice& device, rhi::IShader* computeShader)
    : device_(&device) {
    if (computeShader) {
        auto res = device_->create_compute_pipeline({
            .debugName = "TransformComputePass",
            .computeShader = computeShader,
        });
        if (res.ok()) {
            pipeline_ = std::move(res).value();
        }
    }
}

core::Status TransformComputePass::dispatch(rhi::ICommandBuffer& cmd, const TransformComputePassDesc& desc) {
    if (!pipeline_) {
        return core::Status::failure(core::StatusCode::invalid_state, "TransformComputePass: no valid pipeline");
    }
    
    auto s = cmd.bind_compute_pipeline(*pipeline_);
    if (!s.ok()) {
        return s;
    }

    if (desc.localTransformBuffer) {
        s = cmd.bind_storage_buffer(0, *desc.localTransformBuffer, desc.localTransformOffset);
        if (!s.ok()) {
            return s;
        }
    }
    if (desc.parentIndexBuffer) {
        s = cmd.bind_storage_buffer(1, *desc.parentIndexBuffer, desc.parentIndexOffset);
        if (!s.ok()) {
            return s;
        }
    }
    if (desc.outTransformBuffer) {
        s = cmd.bind_storage_buffer(2, *desc.outTransformBuffer, desc.outTransformOffset);
        if (!s.ok()) {
            return s;
        }
    }

    if (desc.nodeCount > 0) {
        // Assume 64 threads per group
        std::uint32_t group_x = (desc.nodeCount + 63) / 64;
        return cmd.dispatch_compute(group_x, 1, 1);
    }

    return core::Status::success();
}

} // namespace truffle::render

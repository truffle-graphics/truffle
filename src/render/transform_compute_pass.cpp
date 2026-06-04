#include "truffle/render/transform_compute_pass.hpp"
#include "truffle/rhi/shader_reflection.hpp"

#include <array>

namespace truffle::render {

namespace {

core::Status validate_compute_bindings(const TransformComputePassDesc& desc,
                                       const rhi::IComputePipeline& pipeline) {
    const auto* reflection = pipeline.reflection();
    if (!reflection) {
        return core::Status::success();
    }

    constexpr std::size_t kComputeBindingSlots = 3;
    std::array<bool, kComputeBindingSlots> expectedBufferBindings{};
    for (std::size_t i = 0; i < reflection->get_binding_count(); ++i) {
        const auto& binding = reflection->get_binding_info(i);
        if (binding.type == rhi::ResourceBindingType::Buffer &&
            binding.stage == rhi::ShaderStage::compute) {
            if (binding.bindingIndex >= kComputeBindingSlots) {
                return core::Status::failure(
                    core::StatusCode::invalid_argument,
                    "TransformComputePass: reflection binding index exceeds supported range");
            }
            expectedBufferBindings[binding.bindingIndex] = true;
        }
    }

    std::array<bool, kComputeBindingSlots> providedBufferBindings{};
    if (desc.localTransformBuffer) {
        providedBufferBindings[0] = true;
    }
    if (desc.parentIndexBuffer) {
        providedBufferBindings[1] = true;
    }
    if (desc.outTransformBuffer) {
        providedBufferBindings[2] = true;
    }

    for (std::size_t i = 0; i < kComputeBindingSlots; ++i) {
        if (expectedBufferBindings[i] && !providedBufferBindings[i]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "TransformComputePass: missing storage buffer required by pipeline reflection");
        }
    }

    for (std::size_t i = 0; i < kComputeBindingSlots; ++i) {
        if (providedBufferBindings[i] && !expectedBufferBindings[i]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "TransformComputePass: unexpected storage buffer binding not declared by pipeline reflection");
        }
    }

    return core::Status::success();
}

} // namespace

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

    s = validate_compute_bindings(desc, *pipeline_);
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

#include "truffle/render/transform_compute_pass.hpp"

#include <array>
#include <utility>

namespace truffle::render {
namespace {

core::Status validate_compute_bindings(const TransformComputePassDesc& desc,
                                       const rhi::ComputePipeline& pipeline) {
    if (pipeline.reflection().bindings().empty()) {
        return core::Status::success();
    }
    constexpr std::size_t kBindingCount = 3;
    std::array<bool, kBindingCount> expected{};
    for (const auto& binding : pipeline.reflection().bindings()) {
        if (binding.type != rhi::ResourceBindingType::buffer ||
            binding.stage != rhi::ShaderStage::compute) {
            continue;
        }
        if (binding.group != 0 || binding.binding >= kBindingCount) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                "TransformComputePass: reflected binding is outside group 0");
        }
        expected[binding.binding] = true;
    }
    const std::array<bool, kBindingCount> provided{
        desc.localTransformBuffer != nullptr,
        desc.parentIndexBuffer != nullptr,
        desc.outTransformBuffer != nullptr,
    };
    for (std::size_t index = 0; index < kBindingCount; ++index) {
        if (expected[index] != provided[index]) {
            return core::Status::failure(
                core::StatusCode::invalid_argument,
                expected[index]
                    ? "TransformComputePass: missing reflected buffer binding"
                    : "TransformComputePass: unexpected buffer binding");
        }
    }
    return core::Status::success();
}

} // namespace

TransformComputePass::TransformComputePass(rhi::Device& device,
                                           rhi::Shader* computeShader)
    : device_(&device) {
    if (computeShader == nullptr) {
        return;
    }
    auto result = device_->create_compute_pipeline({
        .computeShader = computeShader,
        .debugName = "TransformComputePass",
    });
    if (result.ok()) {
        pipeline_ = std::move(result).value();
    }
}

core::Status TransformComputePass::dispatch(
    rhi::CommandList& list, const TransformComputePassDesc& desc) {
    if (!pipeline_.valid()) {
        return core::Status::failure(core::StatusCode::invalid_state,
                                     "TransformComputePass has no pipeline");
    }
    if (auto status = validate_compute_bindings(desc, pipeline_); !status.ok()) {
        return status;
    }
    auto encoderResult = list.begin_compute();
    if (!encoderResult.ok()) {
        return encoderResult.status();
    }
    auto encoder = std::move(encoderResult).value();
    if (auto status = encoder.bind_pipeline(pipeline_); !status.ok()) {
        return status;
    }
    const std::array<std::pair<rhi::Buffer*, std::size_t>, 3> buffers{{
        {desc.localTransformBuffer, desc.localTransformOffset},
        {desc.parentIndexBuffer, desc.parentIndexOffset},
        {desc.outTransformBuffer, desc.outTransformOffset},
    }};
    for (std::uint32_t index = 0; index < buffers.size(); ++index) {
        if (buffers[index].first != nullptr) {
            if (auto status = encoder.bind_storage_buffer(
                    index, *buffers[index].first, buffers[index].second);
                !status.ok()) {
                return status;
            }
        }
    }
    if (desc.nodeCount != 0) {
        const auto workgroup = pipeline_.preferred_workgroup_size();
        const auto groups = (desc.nodeCount + workgroup.width - 1u) /
                            workgroup.width;
        if (auto status = encoder.dispatch(groups, 1, 1); !status.ok()) {
            return status;
        }
    }
    return encoder.end();
}

} // namespace truffle::render

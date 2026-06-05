#pragma once

#include "truffle/core/status.hpp"
#include "truffle/rhi/rhi.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace truffle::render {

struct TransformComputePassDesc {
    rhi::IBuffer* localTransformBuffer = nullptr;
    std::size_t   localTransformOffset = 0;

    rhi::IBuffer* parentIndexBuffer = nullptr;
    std::size_t   parentIndexOffset = 0;

    rhi::IBuffer* outTransformBuffer = nullptr;
    std::size_t   outTransformOffset = 0;

    std::uint32_t nodeCount = 0;
};

class TransformComputePass {
public:
    explicit TransformComputePass(rhi::IDevice& device, rhi::IShader* computeShader);

    [[nodiscard]] core::Status dispatch(rhi::ICommandBuffer& cmd,
                                        const TransformComputePassDesc& desc);

private:
    rhi::IDevice* device_ = nullptr;
    std::unique_ptr<rhi::IComputePipeline> pipeline_;
};

} // namespace truffle::render

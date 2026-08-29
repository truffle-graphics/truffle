#pragma once

#include "truffle/core/status.hpp"
#include "truffle/rhi/rhi.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace truffle::render {

struct TransformComputePassDesc {
    rhi::Buffer* localTransformBuffer = nullptr;
    std::size_t   localTransformOffset = 0;

    rhi::Buffer* parentIndexBuffer = nullptr;
    std::size_t   parentIndexOffset = 0;

    rhi::Buffer* outTransformBuffer = nullptr;
    std::size_t   outTransformOffset = 0;

    std::uint32_t nodeCount = 0;
};

class TransformComputePass {
public:
    explicit TransformComputePass(rhi::Device& device, rhi::Shader* computeShader);

    [[nodiscard]] core::Status dispatch(rhi::CommandList& cmd,
                                        const TransformComputePassDesc& desc);

private:
    rhi::Device*          device_ = nullptr;
    rhi::ComputePipeline pipeline_;
};

} // namespace truffle::render

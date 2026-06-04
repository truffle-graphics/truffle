#pragma once

#include <string_view>

namespace truffle::render {

constexpr std::string_view kTransformComputeMSL = R"msl(
#include <metal_stdlib>
using namespace metal;

// Hierarchical transform resolution kernel
[[kernel]] void compute_transforms(
    uint id [[thread_position_in_grid]],
    device const float4x4* localTransforms [[buffer(0)]],
    device const int* parentIndices        [[buffer(1)]],
    device float4x4* globalTransforms      [[buffer(2)]])
{
    float4x4 result = localTransforms[id];
    int parentIdx = parentIndices[id];
    
    // Walk up the hierarchy
    // (Assuming parentIndices are acyclic and terminate at -1)
    int max_depth = 1000;
    while (parentIdx >= 0 && max_depth > 0) {
        result = localTransforms[parentIdx] * result;
        parentIdx = parentIndices[parentIdx];
        max_depth--;
    }
    
    globalTransforms[id] = result;
}
)msl";

} // namespace truffle::render

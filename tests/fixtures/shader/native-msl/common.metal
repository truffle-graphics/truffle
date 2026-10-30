#include <metal_stdlib>
using namespace metal;
#ifndef TRUFFLE_MSL_CONFIG
#error MSL configuration was not supplied
#endif
#if TRUFFLE_MSL_CONFIG != 1
#error MSL configuration mismatch
#endif
struct VertexInput {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
};
struct VertexOutput {
    float4 position [[position]];
    float2 uv [[user(locn0)]];
};
struct Parameters {
    float4 tint;
};

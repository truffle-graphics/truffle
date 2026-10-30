#include "common.hlsli"

VertexOutput vertexMain(float2 position : TEXCOORD0) {
    VertexOutput output;
    output.position = float4(position, 0, 1);
    output.tint = configured(float4(1, 1, 1, 1));
    return output;
}

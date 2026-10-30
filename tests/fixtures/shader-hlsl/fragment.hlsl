#include "common.hlsli"

cbuffer Params : register(b7, space1) {
    float4 value;
};

float4 fragmentMain(VertexOutput input) : SV_Target0 {
    return configured(value * input.tint);
}

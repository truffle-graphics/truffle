#ifndef TRUFFLE_HLSL_COMMON
#define TRUFFLE_HLSL_COMMON

#include "configuration.hlsli"

struct VertexOutput {
    float4 position : SV_Position;
    nointerpolation float4 tint : TEXCOORD0;
};

float4 configured(float4 value) {
    return applyScale(value);
}

uint configuredIndex(uint value) {
    return applyQuality(value);
}

#endif

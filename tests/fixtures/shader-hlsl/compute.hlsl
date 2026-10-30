#include "common.hlsli"

cbuffer Params : register(b7, space1) {
    uint4 bias;
};
RWStructuredBuffer<uint> output : register(u9, space1);

[numthreads(2, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    output[id.x] = bias.x + configuredIndex(id.x);
}

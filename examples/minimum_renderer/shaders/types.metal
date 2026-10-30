#include <metal_stdlib>
using namespace metal;

struct AppInput {
    float3 coordinate [[attribute(0)]];
    float3 surfaceDirection [[attribute(1)]];
    float4 centerScale [[attribute(2)]];
    float4 orientation [[attribute(3)]];
    float4 albedo [[attribute(4)]];
};

struct AppOutput {
    float4 position [[position]];
    float4 color [[user(locn0)]];
};

struct AppView {
    float4x4 viewProjection;
    float4 lightDirectionIntensity;
};

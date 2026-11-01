#ifndef TFL_HLSL_CONFIGURATION
#define TFL_HLSL_CONFIGURATION

float4 applyScale(float4 value) {
    return value * SCALE;
}

uint applyQuality(uint value) {
    return value + QUALITY;
}

#endif

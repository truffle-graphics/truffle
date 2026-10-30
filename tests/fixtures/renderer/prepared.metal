#include <metal_stdlib>
using namespace metal;

vertex float4 vertex_main(uint index [[vertex_id]]) {
    const float2 positions[] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    return float4(positions[index], 0.0, 1.0);
}

fragment float4 red_fragment() {
    return float4(1.0, 0.0, 0.0, 1.0);
}

fragment float4 green_fragment() {
    return float4(0.0, 1.0, 0.0, 1.0);
}

fragment float4 tinted_fragment(constant float4& tint [[buffer(0)]]) {
    return tint;
}

struct AttributeInput {
    float4 position [[attribute(0)]];
};

vertex float4 attribute_vertex(AttributeInput input [[stage_in]]) {
    return input.position;
}

#include <metal_stdlib>
using namespace metal;

// Truffle GUI program: one quad per rounded rectangle, shaded with a signed
// distance so edges, corners and borders are anti-aliased in the fragment
// stage. Output is premultiplied alpha.

struct ShapeVertex {
    float2 position [[attribute(0)]]; // clip space
    float2 uv [[attribute(1)]];
    float4 fill [[attribute(2)]];     // straight alpha
    float4 border [[attribute(3)]];   // straight alpha
    float2 local [[attribute(4)]];    // pixels from the rectangle centre
    float4 shape [[attribute(5)]];    // half size, corner radius, border width (pixels)
};

struct ShapeFragment {
    float4 position [[position]];
    float2 uv [[user(locn0)]];
    float4 fill [[user(locn1)]];
    float4 border [[user(locn2)]];
    float2 local [[user(locn3)]];
    float4 shape [[user(locn4)]];
};

vertex ShapeFragment gui_vertex(ShapeVertex input [[stage_in]]) {
    ShapeFragment output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    output.fill = input.fill;
    output.border = input.border;
    output.local = input.local;
    output.shape = input.shape;
    return output;
}

// Signed distance to a rounded box centred at the origin; negative inside.
static float rounded_box(float2 p, float2 half_size, float radius) {
    const float2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

fragment float4 gui_fragment(ShapeFragment input [[stage_in]],
                             texture2d<float> image [[texture(0)]],
                             sampler image_sampler [[sampler(1)]]) {
    const float2 half_size = input.shape.xy;
    const float radius = input.shape.z;
    const float border = input.shape.w;
    const float outside = rounded_box(input.local, half_size, radius);
    const float coverage = clamp(0.5 - outside, 0.0, 1.0);

    float4 fill = image.sample(image_sampler, input.uv) * input.fill;
    fill.rgb *= fill.a;
    float4 color = fill;
    if (border > 0.0) {
        const float inner = rounded_box(input.local, max(half_size - border, 0.0),
                                        max(radius - border, 0.0));
        const float4 edge = float4(input.border.rgb * input.border.a, input.border.a);
        color = mix(edge, fill, clamp(0.5 - inner, 0.0, 1.0));
    }
    return color * coverage;
}

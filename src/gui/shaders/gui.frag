#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 fill;
layout(location = 2) in vec4 border_color;
layout(location = 3) in vec2 local;
layout(location = 4) in vec4 shape;

layout(set = 0, binding = 0) uniform texture2D image;
layout(set = 0, binding = 1) uniform sampler image_sampler;
layout(location = 0) out vec4 output_color;

float rounded_box(vec2 p, vec2 half_size, float radius) {
    const vec2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    const vec2 half_size = shape.xy;
    const float radius = shape.z;
    const float border = shape.w;
    const float outside = rounded_box(local, half_size, radius);
    const float coverage = clamp(0.5 - outside, 0.0, 1.0);

    vec4 sampled_fill = texture(sampler2D(image, image_sampler), uv) * fill;
    sampled_fill.rgb *= sampled_fill.a;
    vec4 color = sampled_fill;
    if (border > 0.0) {
        const float inner = rounded_box(local, max(half_size - border, 0.0),
                                        max(radius - border, 0.0));
        const vec4 edge = vec4(border_color.rgb * border_color.a, border_color.a);
        color = mix(edge, sampled_fill, clamp(0.5 - inner, 0.0, 1.0));
    }
    output_color = color * coverage;
}

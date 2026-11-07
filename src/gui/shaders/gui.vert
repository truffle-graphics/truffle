#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 fill;
layout(location = 3) in vec4 border;
layout(location = 4) in vec2 local;
layout(location = 5) in vec4 shape;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_fill;
layout(location = 2) out vec4 out_border;
layout(location = 3) out vec2 out_local;
layout(location = 4) out vec4 out_shape;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    out_uv = uv;
    out_fill = fill;
    out_border = border;
    out_local = local;
    out_shape = shape;
}

#version 450
#include "modules/color.glsl"

layout(location = 0) out vec4 color;

void main() {
    color = shade();
}

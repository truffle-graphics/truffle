#ifndef TRUFFLE_EXAMPLE_COLOR
#define TRUFFLE_EXAMPLE_COLOR
#include "tint.glsl"

vec4 shade() {
#if USE_TINT
    return apply_tint(vec4(1.0));
#else
    return vec4(1.0);
#endif
}
#endif

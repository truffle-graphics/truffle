float3 rotate_vector(float4 quaternion, float3 value) {
    return value + 2.0 * cross(quaternion.xyz,
                               cross(quaternion.xyz, value) + quaternion.w * value);
}

float3 object_position(AppInput input) {
    return rotate_vector(input.orientation, input.coordinate * input.centerScale.w) +
           input.centerScale.xyz;
}

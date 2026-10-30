vertex AppOutput vertex_main(AppInput input [[stage_in]],
                            constant AppView& parameters [[buffer(16)]]) {
    AppOutput result;
    result.position = parameters.viewProjection * float4(object_position(input), 1.0);
    result.color = directional_color(rotate_vector(input.orientation, input.surfaceDirection),
                                    input.albedo, parameters.lightDirectionIntensity);
    return result;
}

fragment float4 fragment_main(AppOutput input [[stage_in]]) {
    return input.color;
}

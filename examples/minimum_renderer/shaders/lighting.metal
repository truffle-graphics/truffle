float4 directional_color(float3 normal, float4 albedo, float4 light) {
    float diffuse = max(dot(normalize(normal), -normalize(light.xyz)), 0.0);
    return float4(albedo.rgb * diffuse * light.w, albedo.a);
}

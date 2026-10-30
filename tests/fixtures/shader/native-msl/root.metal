vertex VertexOutput vertex_main(VertexInput input [[stage_in]]) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

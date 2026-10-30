import transform;

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    output[id.x] = transformed(id.x);
}

[shader("vertex")]
float4 vertexMain(uint id : SV_VulkanVertexID) : SV_Position
{
    return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target0
{
    return float4(float(transformed(0)) / 1000.0, 0.0, 0.0, 1.0);
}

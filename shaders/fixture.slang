struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(float3 position : POSITION, float3 color : COLOR0)
{
    VertexOutput result;
    result.position = float4(position, 1.0);
    result.color = color;
    return result;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}

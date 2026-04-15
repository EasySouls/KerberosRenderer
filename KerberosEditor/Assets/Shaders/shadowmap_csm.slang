import common;

struct VSInput 
{
    float3 Pos;
    float3 Normal;
};

[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

struct VSOutput
{
    float4 Pos : SV_Position;
};

struct GSOutput
{
    float4 Pos : SV_Position;
    uint RenderTargetArrayIndex : SV_RenderTargetArrayIndex;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(perObject.model, float4(input.Pos, 1.0));
    output.Pos = worldPosition;
    return output;
}

[shader("geometry")]
[maxvertexcount(3)]
void geometryMain(triangle VSOutput input[3], inout TriangleStream<GSOutput> triStream, uint invocationID: SV_GSInstanceID)
{
    for (int i = 0; i < 3; i++)
    {
        GSOutput output;
        output.Pos = mul(scene.lightSpaceMatrices[invocationID], float4(input[i].Pos.xyz, 1.0));
        output.RenderTargetArrayIndex = invocationID;
        triStream.Append(output);
    }
    triStream.RestartStrip();
}

[shader("fragment")]
void fragmentMain(GSOutput input) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
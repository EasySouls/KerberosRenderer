import common;

struct VSInput
{
    float3 Position;
    float3 Color;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Color : TEXCOORD0;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(scene.projection, mul(scene.view, float4(input.Position, 1.0)));
    output.Color = input.Color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return float4(input.Color, 1.0);
}

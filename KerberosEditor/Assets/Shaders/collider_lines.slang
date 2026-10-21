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

struct PSOutput
{
    float4 Color : SV_Target0;
    float Reactive : SV_Target1;
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
PSOutput fragmentMain(VSOutput input)
{
    PSOutput output;
    output.Color = float4(input.Color, 1.0);
    output.Reactive = 1.0;
    return output;
}

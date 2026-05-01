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
    float4 Position : SV_Position;
    float3 ViewNormal : TEXCOORD0;
};

struct PSOutput
{
    float4 Normal : SV_Target0;
}

[shader("vertex")]
VSOutput vertexMain(VSInput input) 
{
    VSOutput output;

    float4 worldPos = mul(perObject.model, float4(input.Pos, 1.0));
    output.Position = mul(scene.projection, mul(scene.view, worldPos));

    float3 worldNormal = mul((float3x3)perObject.model, input.Normal);
    output.ViewNormal = mul((float3x3)scene.view, worldNormal);

    return output;
}

[shader("fragment")]
PSOutput fragmentMain(VSOutput inVert)
{
    PSOutput output;

    float3 N = normalize(inVert.ViewNormal);

    // Mapping normal from [-1, 1] to [0, 1] to store in a UNORM texture
    // If using a SFLOAT texture format (like R16G16B16A16_SFLOAT), just return float4(N, 1.0);
    output.Normal = float4(N * 0.5 + 0.5, 1.0);

    return output;
}
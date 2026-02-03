struct VSInput 
{
    float3 Pos;
    float3 Normal;
};

struct SceneData
{
    float4x4 projection;
    float4x4 view;
    float4x4 lightSpaceMatrix;
    float3 ambientLightColor;
    float3 camPos;
};
[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

struct Material {
    float3 albedo;
    float roughness;
    float metallic;
};

struct PerObject {
    float4x4 model;
    float4x4 worldNormal;
    Material material;
};
[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

struct VSOutput
{
    float4 Pos : SV_Position;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(perObject.model, float4(input.Pos, 1.0));
    output.Pos = mul(scene.lightSpaceMatrix, worldPosition);
    return output;
}

[shader("fragment")]
void fragMain(VSOutput inVert) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
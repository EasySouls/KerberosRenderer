struct VSInput {
    float3 inPosition;
    float3 inNormal;
};

struct SceneData
{
    float4x4 projection;
    float4x4 view;
    float4x4 lightSpaceMatrix;
    float3 camPos;
};
[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

struct Material {
    [[vk::offset(12)]] float roughness;
    [[vk::offset(16)]] float metallic;
    [[vk::offset(20)]] float r;
    [[vk::offset(24)]] float g;
    [[vk::offset(28)]] float b;
};

struct PerObject {
    float3 objPos;
    float4x4 model;
    float4x4 worldNormal;
    Material material;
};
[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

struct VSOutput
{
    float4 pos : SV_Position;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(perObject.model, float4(input.inPosition, 1.0));
    output.pos = mul(scene.lightSpaceMatrix, worldPosition);
    return output;
}

[shader("fragment")]
void fragMain(VSOutput inVert) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
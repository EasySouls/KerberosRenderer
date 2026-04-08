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

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(perObject.model, float4(input.Pos, 1.0));
    output.Pos = mul(scene.lightSpaceMatrix, worldPosition);
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput inVert) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
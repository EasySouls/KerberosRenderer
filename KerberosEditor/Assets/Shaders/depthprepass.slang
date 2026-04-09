import common;

struct VSInput 
{
    float3 Pos;
};

[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

struct VSOutput
{
    float4 Position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) 
{
    VSOutput output;
    float4 worldPos = mul(perObject.model, float4(input.Pos, 1.0));
    output.Position = mul(scene.projection, mul(scene.view, worldPos));
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput inVert) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
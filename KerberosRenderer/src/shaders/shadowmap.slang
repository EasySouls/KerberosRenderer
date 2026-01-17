struct VSInput {
    float3 inPosition;
    float3 inNormal;
};

struct UniformBuffer {
    float4x4 lightSpaceMatrix;
    float4x4 model;
};

[[vk::binding(0)]]
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput
{
    float4 pos : SV_Position;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(ubo.model, float4(input.inPosition, 1.0));
    output.pos = mul(ubo.lightSpaceMatrix, worldPosition);
    return output;
}

[shader("fragment")]
void fragMain(VSOutput inVert) : SV_TARGET
{
    //// Depth value is automatically written to the depth buffer
    //return 0.0;
}
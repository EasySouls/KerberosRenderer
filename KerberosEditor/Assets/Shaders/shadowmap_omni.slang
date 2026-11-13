import common;

struct VSInput
{
    float3 Pos;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord;
};

struct PushConstants
{
    float4x4 viewProjection;
};
[[vk::push_constant]]
PushConstants pushConstants;

[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(0, 1)]]
Texture2D globalTextures[];
[[vk::binding(1, 1)]]
SamplerState globalSamplers[];

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

float4 SampleBindless(uint packedIndex, float2 uv)
{
    uint texIdx = packedIndex & 0x00FFFFFF;
    uint sampIdx = packedIndex >> 24;
    if (texIdx == 0)
        return float4(1.0, 0.0, 1.0, 1.0);
    return globalTextures[texIdx].Sample(globalSamplers[sampIdx], uv);
}

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(perObject.model, float4(input.Pos, 1.0));
    output.Pos = mul(pushConstants.viewProjection, worldPosition);
    output.TexCoord = input.TexCoord;
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput input) : SV_Target
{
    if (perObject.alphaMode == 1u) {
        float4 albedoTex = SampleBindless(perObject.material.albedoTexIndex, input.TexCoord);
        float alpha = albedoTex.a * perObject.material.albedo.a;
        if (alpha < perObject.alphaCutoff)
            discard;
    }
}
import common;

struct VSInput 
{
    float3 Pos;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(0, 1)]]
Texture2D globalTextures[];
[[vk::binding(1, 1)]]
SamplerState globalSamplers[];

// 0 = depth-only (opaque casters), 1 = alpha-tested (masked casters).
[SpecializationConstant] const int enableAlphaTest = 1;

struct PushConstants
{
    uint cascadeIndex;
};
[[vk::push_constant]]
PushConstants pushConstants;

struct VSOutput
{
    float4 Pos : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

float4 SampleBindless(uint packedIndex, float2 uv) {
    uint texIdx = packedIndex & 0x00FFFFFF;
    uint sampIdx = packedIndex >> 24;

    if (texIdx == 0) {
        return float4(1.0, 0.0, 1.0, 1.0);
    }

    return globalTextures[texIdx].Sample(globalSamplers[sampIdx], uv);
}

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(perObject.model, float4(input.Pos, 1.0));
    output.Pos = mul(scene.lightSpaceMatrices[pushConstants.cascadeIndex], worldPosition);
    output.TexCoord = input.TexCoord;
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput input) : SV_Target
{
    // Masked casters sample the base-color alpha so cutout geometry casts a
    // correct shadow instead of a solid quad. This whole block is compiled out
    // of the opaque variant, which lets it keep early depth testing.
    if (enableAlphaTest != 0) {
        float4 albedoTex = SampleBindless(perObject.material.albedoTexIndex, input.TexCoord);
        float alpha = albedoTex.a * perObject.material.albedo.a;
        if (alpha < perObject.alphaCutoff)
            discard;
    }
}

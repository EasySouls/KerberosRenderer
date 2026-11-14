import common;

struct VSInput 
{
    float3 Pos;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord;
};

[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(0, 1)]]
Texture2D globalTextures[]; 
[[vk::binding(1, 1)]]
SamplerState globalSamplers[];

// 0 = depth-only (opaque geometry), 1 = alpha-tested (masked geometry).
[SpecializationConstant] const int enableAlphaTest = 1;

struct VSOutput
{
    float4 Position : SV_Position;
    float3 ViewNormal : TEXCOORD0;
    float2 Motion : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

struct PSOutput
{
    float4 Normal : SV_Target0;
    float2 Motion : SV_Target1;
}

float4 SampleBindless(uint packedIndex, float2 uv) {
    uint texIdx = packedIndex & 0x00FFFFFF;
    uint sampIdx = packedIndex >> 24;

    if (texIdx == 0) {
        return float4(1.0, 0.0, 1.0, 1.0);
    }

    return globalTextures[texIdx].Sample(globalSamplers[sampIdx], uv);
}

[shader("vertex")]
VSOutput vertexMain(VSInput input) 
{
    VSOutput output;

    float4 worldPos = mul(perObject.model, float4(input.Pos, 1.0));
    output.Position = mul(scene.projection, mul(scene.view, worldPos));

    // Motion vectors are computed in unjittered space. FSR3 receives the
    // sub-pixel jitter separately, so the motion buffer must not contain it.
    float4 currentClipPosition = mul(scene.motionProjection, mul(scene.view, worldPos));
    float4 previousClipPosition = mul(scene.previousProjection,
                                      mul(scene.previousView,
                                          mul(perObject.previousModel, float4(input.Pos, 1.0))));
    float2 currentNdc = currentClipPosition.xy / currentClipPosition.w;
    float2 previousNdc = previousClipPosition.xy / previousClipPosition.w;
    output.Motion = (previousNdc - currentNdc) * 0.5 * scene.viewportSize;

    float3 worldNormal = mul((float3x3)perObject.worldNormal, input.Normal);
    output.ViewNormal = mul((float3x3)scene.view, worldNormal);
    output.TexCoord = input.TexCoord;

    return output;
}

[shader("fragment")]
PSOutput fragmentMain(VSOutput inVert)
{
    // Alpha-masked geometry must cut out the same fragments here as it does in
    // the opaque and shadow passes, otherwise the prepass depth/normal/motion
    // buffers disagree with the visible surface. The opaque variant compiles
    // this block out entirely.
    if (enableAlphaTest != 0) {
        float4 albedoTex = SampleBindless(perObject.material.albedoTexIndex, inVert.TexCoord);
        float alpha = albedoTex.a * perObject.material.albedo.a;
        if (alpha < perObject.alphaCutoff)
            discard;
    }

    PSOutput output;

    float3 N = normalize(inVert.ViewNormal);

    // Mapping normal from [-1, 1] to [0, 1] to store in a UNORM texture
    // If using a SFLOAT texture format (like R16G16B16A16_SFLOAT), just return float4(N, 1.0);
    output.Normal = float4(N * 0.5 + 0.5, 1.0);
    output.Motion = inVert.Motion;

    return output;
}
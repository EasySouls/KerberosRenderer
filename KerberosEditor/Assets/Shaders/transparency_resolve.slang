import common;

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Sampler2D<float4> texOpaqueColor : register(t0);

[[vk::binding(1, 0)]]
Sampler2D<float> texOpaqueDepth : register(t1);

[[vk::binding(2, 0)]]
Sampler2D<float4> texAccumulation : register(t2);

[[vk::binding(3, 0)]]
Sampler2D<float> texRevealage   : register(t3);

[[vk::binding(4, 0)]] 
Sampler2D<float2> texDistortion : register(t4);


[shader("vertex")]
VSOutput vertexMain(uint vertexID: SV_VertexID) 
{
    VSOutput output;

    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);

    return output;
}

[shader("fragment")]
float4 resolveMain(VSOutput input) : SV_Target
{
    float2 uv = input.TexCoord;
    float4 accum = texAccumulation.Sample(uv);
    float revealage = texRevealage.Sample(uv).r;
    float2 distortion = texDistortion.Sample(uv).rg;

    // TODO: maybe clamping between 0 and 1
    float2 distortedUV = uv + distortion;

    float opaqueDepth = texOpaqueDepth.Sample(uv).r;
    float distortedDepth = texOpaqueDepth.Sample(distortedUV).r;

    // If distorted pixel is closer to camera than the original pixel, cancel distortion
    if (distortedDepth < opaqueDepth) {
        distortedUV = uv;
    }

    float4 background = texOpaqueColor.Sample(distortedUV);

    // Suppress potential overflow/infinite artifacts
    if (isinf(max(max(accum.r, accum.g), accum.b))) {
        accum.rgb = float3(accum.a);
    }

    float3 transparentColor = accum.rgb / max(accum.a, 0.00001f);

    float3 sceneColor = transparentColor + (background.rgb * revealage);
    float bloomMask = background.a;

    return float4(sceneColor, bloomMask);
}
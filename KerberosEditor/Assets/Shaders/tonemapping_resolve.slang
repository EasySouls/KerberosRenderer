import common;
import tonemapping;

// Input: HDR scene color + bloom
[[vk::binding(0, 0)]] Texture2D<float4> sceneColorTex;
[[vk::binding(1, 0)]] SamplerState linearSampler;

[[vk::binding(2, 0)]] Sampler2D<float4> bloomSampler;

[[vk::binding(3, 0)]]
ConstantBuffer<GlobalLighting> globalLighting;

struct PushConstants {
    float2 inverseScreenSize;
    float bloomIntensity;
    float tonemapOperator;
};
[[vk::push_constant]] 
PushConstants pc;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float2 uv = input.uv;

    // Bloom is already stored in exposed units by the bloom extract pass.
    // Apply exposure to the scene here so both contributions use the same
    // photometric scale, then tone map without applying exposure a second time.
    float3 sceneColor = sceneColorTex.Sample(linearSampler, uv).rgb * globalLighting.lightingParams.x;
    float3 bloomColor = bloomSampler.Sample(uv).rgb;

    float3 finalHDR = sceneColor + (bloomColor * pc.bloomIntensity);

    float3 ldrColor = saturate(ApplyToneMapping(finalHDR, 1.0f, pc.tonemapOperator));

    ldrColor = ApplyGammaCorrection(ldrColor, globalLighting.lightingParams.z);

    return float4(ldrColor, 1.0);
}

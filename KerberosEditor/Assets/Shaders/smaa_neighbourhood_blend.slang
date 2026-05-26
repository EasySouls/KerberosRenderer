#define SMAA_HLSL_4_1
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

struct PushConstants { float4 rtMetrics; };
[[vk::push_constant]] PushConstants pc;
#define SMAA_RT_METRICS pc.rtMetrics

#include "SMAA.hlsl"

[[vk::binding(0, 0)]] Texture2D<float4> colorTex; // The original Tonemapped image
[[vk::binding(1, 0)]] Texture2D<float4> blendTex; // The output from Pass 2 (Blend weight calculation)
[[vk::binding(2, 0)]] SamplerState LinearSampler;
[[vk::binding(3, 0)]] SamplerState PointSampler;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 offset : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    
    SMAANeighborhoodBlendingVS(output.uv, output.offset);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return SMAANeighborhoodBlendingPS(input.uv, input.offset, colorTex, blendTex);
}
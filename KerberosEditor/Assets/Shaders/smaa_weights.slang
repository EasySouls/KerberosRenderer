#define SMAA_HLSL_4_1
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

struct PushConstants { float4 rtMetrics; };
[[vk::push_constant]] PushConstants pc;
#define SMAA_RT_METRICS pc.rtMetrics

#include "SMAA.hlsl"

[[vk::binding(0, 0)]] Texture2D<float4> edgesTex;
[[vk::binding(1, 0)]] Texture2D<float4> areaTex;
[[vk::binding(2, 0)]] Texture2D<float4> searchTex;
[[vk::binding(3, 0)]] SamplerState LinearSampler;
[[vk::binding(4, 0)]] SamplerState PointSampler;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 pixcoord : TEXCOORD1;
    float4 offsets[3] : TEXCOORD5;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    
    SMAABlendingWeightCalculationVS(output.uv, output.pixcoord, output.offsets);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return SMAABlendingWeightCalculationPS(
        input.uv, input.pixcoord, input.offsets, 
        edgesTex, areaTex, searchTex, float4(0,0,0,0) // Subsmaple indices are 0 for standard SMAA
    );
}
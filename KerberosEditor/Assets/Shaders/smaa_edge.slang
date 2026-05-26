#define SMAA_HLSL_4_1
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

struct PushConstants {
    float4 rtMetrics; // (1/w, 1/h, w, h)
};
[[vk::push_constant]] PushConstants pc;
#define SMAA_RT_METRICS pc.rtMetrics

#include "SMAA.hlsl"

[[vk::binding(0, 0)]] Texture2D<float4> colorTex;
[[vk::binding(1, 0)]] SamplerState LinearSampler;
[[vk::binding(2, 0)]] SamplerState PointSampler;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 offsets[3] : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    
    // SMAA requires precalculated offsets in the vertex shader for performance
    SMAAEdgeDetectionVS(output.uv, output.offsets);
    return output;
}

[shader("fragment")]
float2 fragmentMain(VSOutput input) : SV_Target
{
    // You can also use SMAADepthEdgeDetectionPS if you prefer Depth-based edges
    return SMAAColorEdgeDetectionPS(input.uv, input.offsets, colorTex);
}
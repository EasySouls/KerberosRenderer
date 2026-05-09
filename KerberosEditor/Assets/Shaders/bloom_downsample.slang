[[vk::binding(0, 0)]] 
Sampler2D srcTexture;

[[vk::binding(1, 0)]] 
RWTexture2D<float4> dstTexture;

[[vk::push_constant]]
cbuffer PushConstants 
{
    float2 srcTexelSize; // 1.0 / srcResolution
    float threshold;
    float knee;
    uint enablePrefilter;
    uint isExtract;
    float maxBrightness;
};

float ComputePrefilter(float3 color, float bloomThreshold, float bloomKnee)
{
    float brightness = max(color.r, max(color.g, color.b));
    float safeBrightness = max(brightness, 1e-5);

    float safeKnee = max(bloomKnee, 1e-5);
    float kneeStart = bloomThreshold - safeKnee;
    float soft = saturate((brightness - kneeStart) / (2.0 * safeKnee));
    soft = soft * soft * safeKnee;

    float hard = max(brightness - bloomThreshold, 0.0);
    float contribution = max(hard, soft) / safeBrightness;
    return contribution;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width, height;
    dstTexture.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height) return;

    float2 uv = (float2(threadId.xy) + 0.5) * (1.0 / float2(width, height));
    float x = srcTexelSize.x;
    float y = srcTexelSize.y;

    // Take 13 samples around the center
    float3 a = srcTexture.SampleLevel(float2(uv.x - 2*x, uv.y + 2*y), 0).rgb;
    float3 b = srcTexture.SampleLevel(float2(uv.x,       uv.y + 2*y), 0).rgb;
    float3 c = srcTexture.SampleLevel(float2(uv.x + 2*x, uv.y + 2*y), 0).rgb;

    float3 d = srcTexture.SampleLevel(float2(uv.x - 2*x, uv.y), 0).rgb;
    float4 e = srcTexture.SampleLevel(float2(uv.x,       uv.y), 0);
    float3 f = srcTexture.SampleLevel(float2(uv.x + 2*x, uv.y), 0).rgb;

    float3 g = srcTexture.SampleLevel(float2(uv.x - 2*x, uv.y - 2*y), 0).rgb;
    float3 h = srcTexture.SampleLevel(float2(uv.x,       uv.y - 2*y), 0).rgb;
    float3 i = srcTexture.SampleLevel(float2(uv.x + 2*x, uv.y - 2*y), 0).rgb;

    float3 j = srcTexture.SampleLevel(float2(uv.x - x, uv.y + y), 0).rgb;
    float3 k = srcTexture.SampleLevel(float2(uv.x + x, uv.y + y), 0).rgb;
    float3 l = srcTexture.SampleLevel(float2(uv.x - x, uv.y - y), 0).rgb;
    float3 m = srcTexture.SampleLevel(float2(uv.x + x, uv.y - y), 0).rgb;

    // Apply weights
    float3 color = e.rgb * 0.125;
    color += (a + c + g + i) * 0.03125;
    color += (b + d + f + h) * 0.0625;
    color += (j + k + l + m) * 0.125;

    if (enablePrefilter != 0)
    {
        color *= ComputePrefilter(color, threshold, knee);
    }

    if (isExtract == 1)
    {   
        color = color * e.a;

        color = min(color, float3(maxBrightness));
    }

    // Optional: Max clamp here to prevent infinite brightness (fireflies)
    // color = min(color, float3(1000.0));

    dstTexture[threadId.xy] = float4(color, 1.0);
}

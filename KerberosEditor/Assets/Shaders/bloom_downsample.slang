[[vk::binding(0, 0)]] 
Sampler2D srcTexture;

[[vk::binding(1, 0)]] 
RWTexture2D<float4> dstTexture;

[[vk::push_constant]]
cbuffer PushConstants 
{
    float2 srcTexelSize; // 1.0 / srcResolution
};

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
    float3 e = srcTexture.SampleLevel(float2(uv.x,       uv.y), 0).rgb;
    float3 f = srcTexture.SampleLevel(float2(uv.x + 2*x, uv.y), 0).rgb;

    float3 g = srcTexture.SampleLevel(float2(uv.x - 2*x, uv.y - 2*y), 0).rgb;
    float3 h = srcTexture.SampleLevel(float2(uv.x,       uv.y - 2*y), 0).rgb;
    float3 i = srcTexture.SampleLevel(float2(uv.x + 2*x, uv.y - 2*y), 0).rgb;

    float3 j = srcTexture.SampleLevel(float2(uv.x - x, uv.y + y), 0).rgb;
    float3 k = srcTexture.SampleLevel(float2(uv.x + x, uv.y + y), 0).rgb;
    float3 l = srcTexture.SampleLevel(float2(uv.x - x, uv.y - y), 0).rgb;
    float3 m = srcTexture.SampleLevel(float2(uv.x + x, uv.y - y), 0).rgb;

    // Apply weights
    float3 color = e * 0.125;
    color += (a + c + g + i) * 0.03125;
    color += (b + d + f + h) * 0.0625;
    color += (j + k + l + m) * 0.125;

    // Optional: Max clamp here to prevent infinite brightness (fireflies)
    // color = min(color, float3(1000.0));

    dstTexture[threadId.xy] = float4(color, 1.0);
}
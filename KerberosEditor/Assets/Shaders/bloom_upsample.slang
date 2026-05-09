[[vk::binding(0, 0)]] Sampler2D srcTexture;
[[vk::binding(1, 0)]] RWTexture2D<float4> dstTexture;

[[vk::push_constant]]
cbuffer PushConstants 
{
    float filterRadius;
};

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width, height;
    dstTexture.GetDimensions(width, height);
    if (threadId.x >= width || threadId.y >= height) return;

    float2 uv = (float2(threadId.xy) + 0.5) * (1.0 / float2(width, height));
    uint srcWidth, srcHeight;
    srcTexture.GetDimensions(srcWidth, srcHeight);
    float2 srcTexelSize = 1.0 / float2(srcWidth, srcHeight);
    
    // 9-tap Tent Filter
    float x = filterRadius * srcTexelSize.x;
    float y = filterRadius * srcTexelSize.y;

    float3 a = srcTexture.SampleLevel(float2(uv.x - x, uv.y + y), 0).rgb;
    float3 b = srcTexture.SampleLevel(float2(uv.x,     uv.y + y), 0).rgb;
    float3 c = srcTexture.SampleLevel(float2(uv.x + x, uv.y + y), 0).rgb;

    float3 d = srcTexture.SampleLevel(float2(uv.x - x, uv.y), 0).rgb;
    float3 e = srcTexture.SampleLevel(float2(uv.x,     uv.y), 0).rgb;
    float3 f = srcTexture.SampleLevel(float2(uv.x + x, uv.y), 0).rgb;

    float3 g = srcTexture.SampleLevel(float2(uv.x - x, uv.y - y), 0).rgb;
    float3 h = srcTexture.SampleLevel(float2(uv.x,     uv.y - y), 0).rgb;
    float3 i = srcTexture.SampleLevel(float2(uv.x + x, uv.y - y), 0).rgb;

    float3 upsampled = e * 0.25;
    upsampled += (b + d + f + h) * 0.125;
    upsampled += (a + c + g + i) * 0.0625;

    // ADD to the existing destination data (which is the downsampled data from earlier)
    float3 current = dstTexture[threadId.xy].rgb;
    dstTexture[threadId.xy] = float4(current + upsampled, 1.0);
}

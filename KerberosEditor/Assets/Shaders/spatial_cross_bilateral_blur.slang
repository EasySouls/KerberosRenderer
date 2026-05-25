struct BlurConstants
{
    float2 inverseResolution; // 1.0 / windowSize
    float2 blurDirection;     // (1, 0) for Horizontal pass, (0, 1) for Vertical pass
};

[[vk::binding(0, 0)]] Sampler2D<float> inputAO;
[[vk::binding(1, 0)]] Sampler2D<float> depthTex;
[[vk::binding(2, 0)]] Sampler2D<float4> normalTex;
[[vk::binding(3, 0)]] RWTexture2D<float> outAO;

[[vk::push_constant]] BlurConstants pc;

float3 GetNormal(float2 uv) {
    float3 encodedNormal = normalTex.SampleLevel(uv, 0).rgb;
    // Map back from [0, 1] to [-1, 1]
    return normalize(encodedNormal * 2.0 - 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    float2 uv = (float2(dispatchThreadID.xy) + 0.5) * pc.inverseResolution;

    // Center pixel data
    float centerAO = inputAO.SampleLevel(uv, 0);
    float centerDepth = depthTex.SampleLevel(uv, 0);
    float3 centerNormal = GetNormal(uv);

    // If it's the skybox/far plane, just pass it through
    if (centerDepth == 1.0) { 
        outAO[dispatchThreadID.xy] = centerAO;
        return;
    }

    float totalAO = centerAO;
    float totalWeight = 1.0;

    // 5-tap separable kernel
    const int RADIUS = 2;
    
    // Simple Gaussian weights
    float spatialWeights[3] = { 1.0, 0.8, 0.4 }; 

    for (int i = -RADIUS; i <= RADIUS; ++i)
    {
        if (i == 0) continue;

        float2 sampleUV = uv + (pc.blurDirection * float(i) * pc.inverseResolution);
        
        float sampleAO = inputAO.SampleLevel(sampleUV, 0);
        float sampleDepth = depthTex.SampleLevel(sampleUV, 0);
        float3 sampleNormal = GetNormal(sampleUV);

        // How far the pixel is
        float weight = spatialWeights[abs(i)];

        // Is it on the same surface plane
        float depthDiff = abs(sampleDepth - centerDepth);
        float depthWeight = exp(-depthDiff * 100.0);

        // Is it facing the same direction
        float normalWeight = pow(max(0.0, dot(centerNormal, sampleNormal)), 16.0);

        float finalWeight = weight * depthWeight * normalWeight;

        totalAO += sampleAO * finalWeight;
        totalWeight += finalWeight;
    }

    outAO[dispatchThreadID.xy] = totalAO / totalWeight;
}
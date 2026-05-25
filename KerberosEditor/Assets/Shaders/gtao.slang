
struct GTAOConstants
{
    float4x4 projectionMatrix;
    float4x4 invProjectionMatrix;
    float2 viewportSize;
    float radius;         // World space radius of AO
    float falloff;        // Distance falloff
    float sampleCount;    // Steps per direction (usually 4-8)
    float directionCount; // Number of directions (usually 2-4)
    float temporalIndex;  // For jittering over time
};

[[vk::binding(0, 0)]]
ConstantBuffer<GTAOConstants> cb;

[[vk::binding(1, 0)]]
Sampler2D<float> depthTex;

[[vk::binding(2, 0)]]
Sampler2D<float4> normalTex;

[[vk::binding(3, 0)]]
RWTexture2D<float> outAO;

float3 GetNormal(float2 uv) {
    float3 encodedNormal = normalTex.SampleLevel(uv, 0).rgb;
    // Map back from [0, 1] to [-1, 1]
    return normalize(encodedNormal * 2.0 - 1.0);
}

float3 GetViewPos(float2 uv)
{
    float depth = depthTex.SampleLevel(uv, 0);
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);

    float4 viewPos = mul(cb.invProjectionMatrix, clipPos);
    return viewPos.xyz / viewPos.w;
}

float IntegrateArc(float h, float n)
{
    return (cos(n) + 2.0 * h * sin(n) - cos(2.0 * h - n)) / 4.0;
}

float InterleavedGradientNoise(float2 pixelCoords)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixelCoords, magic.xy)));
}

float ComputeGTAO(float2 uv, float3 viewPos, float3 viewNormal, float spatialNoise)
{
    float3 viewV = normalize(-viewPos);
    float visibility = 0.0;
    int directionCount = max(1, (int)cb.directionCount);
    int sampleCount = max(1, (int)cb.sampleCount);

    float rotation = (cb.temporalIndex + spatialNoise) * 0.618;

    float2 focalLength = abs(float2(cb.projectionMatrix[0][0], cb.projectionMatrix[1][1]));
    float viewDepth = max(-viewPos.z, 1e-4);
    float2 radiusUV = 0.5 * (cb.radius * focalLength) / viewDepth;

    // Prevent radiusUV from exploding when up close.
    // In this case roughly 200 pixels of the view space
    float maxRadiusUV = 200.0 / cb.viewportSize.y;
    radiusUV = min(radiusUV, maxRadiusUV);

    float falloff2 = max(cb.falloff * cb.falloff, 1e-6);

    for (int d = 0; d < directionCount; ++d)
    {
        float angle = (float(d) + rotation) * (3.14159 / directionCount);
        float2 dir = float2(cos(angle), sin(angle));

        float h1 = -1.0;
        float h2 = -1.0;

        for (int s = 1; s <= sampleCount; ++s)
        {
            float stepJitter = (float(s) - spatialNoise) / sampleCount;
            float2 offset = dir * stepJitter * radiusUV;
            float2 uv1 = uv + offset;
            float2 uv2 = uv - offset;

            // Forward direction
            if (uv1.x > 0.0 && uv1.x < 1.0 && uv1.y > 0.0 && uv1.y < 1.0)
            {
                float3 p1 = GetViewPos(uv1);
                float3 v1 = p1 - viewPos;
                float d2_1 = dot(v1, v1);

                float cosHorizon = dot(viewV, v1 * rsqrt(max(d2_1, 1e-6)));

                // Thickness heuristic
                float distanceFraction = d2_1 / falloff2;
                float weight = saturate(1.0 - distanceFraction);
                cosHorizon = lerp(-1.0, cosHorizon, weight);

                // Keep the highest horizon found so far
                h1 = max(h1, cosHorizon);
            }

            // Opposite direction
            if (uv2.x > 0.0 && uv2.x < 1.0 && uv2.y > 0.0 && uv2.y < 1.0)
            {
                float3 p2 = GetViewPos(uv2);
                float3 v2 = p2 - viewPos;
                float d2_2 = dot(v2, v2);

                float cosHorizon = dot(viewV, v2 * rsqrt(max(d2_2, 1e-6)));

                // Thickness heuristic
                float distanceFraction = d2_2 / falloff2;
                float weight = saturate(1.0 - distanceFraction);
                cosHorizon = lerp(-1.0, cosHorizon, weight);

                // Keep the highest horizon found so far
                h2 = max(h2, cosHorizon);
            }
        }

        h1 = acos(h1);
        h2 = acos(h2);

        // Project normal onto the plane of the search direction
        float3 planeNormal = normalize(cross(float3(dir, 0), viewV));
        float3 tangent = cross(viewV, planeNormal);
        float3 projectedNormal = viewNormal - planeNormal * dot(viewNormal, planeNormal);

        float n = atan2(dot(projectedNormal, tangent), dot(projectedNormal, viewV));

        // Clamp horizons to the valid hemisphere defined by the normal
        h1 = n + clamp(h1 - n, -1.570796, 1.570796);
        h2 = -n + clamp(h2 + n, -1.570796, 1.570796);

        visibility += IntegrateArc(h1, n) + IntegrateArc(h2, -n);
    }

    return saturate(visibility / directionCount);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / cb.viewportSize;

    if (uv.x >= 1.0 || uv.y >= 1.0) return;

    float3 viewPos = GetViewPos(uv);

    // Don't process far plane/skybox
    //if (viewPos.z > 1000.0) {
    //    outAO[dispatchThreadID.xy] = 1.0;
    //    return;
    //}

    float3 viewNormal = GetNormal(uv);

    float noise = InterleavedGradientNoise(float2(dispatchThreadID.xy));

    float ao = ComputeGTAO(uv, viewPos, viewNormal, noise);

    outAO[dispatchThreadID.xy] = ao;
}

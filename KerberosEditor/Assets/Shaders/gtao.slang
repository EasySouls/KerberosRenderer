
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

float ComputeGTAO(float2 uv, float3 viewPos, float3 viewNormal)
{
    float3 viewV = normalize(-viewPos); // Vector to camera
    float occlusion = 0.0;

    // Use a simple hash or blue noise to rotate directions
    float rotation = cb.temporalIndex * 0.618;

    // This converts world-space meters into UV-space 0..1 units
    // cb.projectionMatrix[0][0] is the focal length
    float2 radiusUV = (cb.radius * float2(cb.projectionMatrix[0][0], cb.projectionMatrix[1][1])) / -viewPos.z;

    for (int d = 0; d < (int)cb.directionCount; ++d)
    {
        // 1. Determine the search direction in screen space
        float angle = (float(d) + rotation) * (3.14159 / cb.directionCount);
        float2 dir = float2(cos(angle), sin(angle));

        // Horizon angles (initialized to cosine of the hemisphere)
        float h1 = -1.0;
        float h2 = -1.0;

        // 2. March in two opposite directions along the line
        for (int s = 1; s <= (int)cb.sampleCount; ++s)
        {
            // Convert world radius to screen-space radius (in pixels)
            // cb.projectionMatrix[0][0] is the focal length (fx)
            float pixelRadius = (cb.radius * cb.projectionMatrix[0][0] * cb.viewportSize.x) / -viewPos.z;
            float2 offset = dir * (float(s) / cb.sampleCount) * radiusUV;

            // Sample neighbors
            float3 p1 = GetViewPos(uv + offset / cb.viewportSize);
            float3 p2 = GetViewPos(uv - offset / cb.viewportSize);

            float3 v1 = p1 - viewPos;
            float3 v2 = p2 - viewPos;

            // Calculate elevation angle (clamped by distance falloff)
            float d2_1 = dot(v1, v1);
            float d2_2 = dot(v2, v2);

            // Horizon 1
            h1 = max(h1, dot(viewV, normalize(v1)));
            // Horizon 2
            h2 = max(h2, dot(viewV, normalize(v2)));
        }

        // 3. Convert horizons to actual occlusion using the surface normal
        // This is the "Ground Truth" part: integrating the projected area
        h1 = acos(h1);
        h2 = acos(h2);

        // Project normal onto the plane of the search direction
        float3 planeNormal = normalize(cross(float3(dir, 0), viewV));
        float3 tangent = cross(viewV, planeNormal);
        float3 projectedNormal = viewNormal - planeNormal * dot(viewNormal, planeNormal);

        float n = atan2(dot(projectedNormal, tangent), dot(projectedNormal, viewV));

        // The GTAO integral formula
        occlusion += (cos(n) + 2.0 * sin(h1) * sin(h1 - n) - cos(2.0 * h1 - n)) / 4.0;
        occlusion += (cos(n) + 2.0 * sin(h2) * sin(h2 - n) - cos(2.0 * h2 - n)) / 4.0;
    }

    return saturate(occlusion / cb.directionCount);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / cb.viewportSize;

    if (uv.x > 1.0 || uv.y > 1.0) return;

    float3 viewPos = GetViewPos(uv);

    // Don't process far plane/skybox
    //if (viewPos.z > 1000.0) {
    //    outAO[dispatchThreadID.xy] = 1.0;
    //    return;
    //}

    float3 viewNormal = GetNormal(uv);

    float ao = ComputeGTAO(uv, viewPos, viewNormal);

    outAO[dispatchThreadID.xy] = ao;
}

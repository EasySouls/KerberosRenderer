import common;
import lights;
import pbr_helpers;
import ao;

struct VSInput
{
    float3 Pos;
    float3 Normal;
    float4 Tangent;
    float2 TexCoord;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 FragPos_WorldSpace;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    float2 TexCoord : TEXCOORD0;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(1, 0)]]
ConstantBuffer<GlobalLighting> globalLighting;

[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

struct PSOutput
{
    float4 Color : SV_Target0;
    uint EntityID : SV_Target1;
};

[[vk::binding(3, 0)]]
Sampler2D shadowMap;

[[vk::binding(4, 0)]]
SamplerCube samplerIrradiance;
[[vk::binding(5, 0)]]
Sampler2D samplerBRDFLUT;
[[vk::binding(6, 0)]]
SamplerCube prefilteredMapSampler;

[[vk::binding(7, 0)]]
RaytracingAccelerationStructure accelerationStructure;

[[vk::binding(8, 0)]]
StructuredBuffer<GPULight> sceneLights;

[[vk::binding(9, 0)]]
Sampler2D<float> globalAoTexture;

[[vk::binding(0, 1)]]
Sampler2D albedoMapSampler;
[[vk::binding(1, 1)]]
Sampler2D normalMapSampler;
[[vk::binding(2, 1)]]
Sampler2D roughnessMapSampler;
[[vk::binding(3, 1)]]
Sampler2D metallicMapSampler;
[[vk::binding(4, 1)]]
Sampler2D aoMapSampler;
[[vk::binding(5, 1)]]
Sampler2D emissiveMapSampler;

[SpecializationConstant] const int enableSoftShadows = 0;

static const float PI = 3.14159265359;

float3 prefilteredReflection(float3 R, float roughness)
{
    const float MAX_REFLECTION_LOD = 9.0; // todo: param/const
    float lod = roughness * MAX_REFLECTION_LOD;
    float lodf = floor(lod);
    float lodc = ceil(lod);
    float3 a = prefilteredMapSampler.SampleLevel(R, lodf).rgb;
    float3 b = prefilteredMapSampler.SampleLevel(R, lodc).rgb;
    return lerp(a, b, lod - lodf);
}

float3 CalculateNormal(VSOutput input)
{
    float3 T = normalize(input.Tangent);
    float3 N = normalize(input.Normal);
    float3 B = normalize(input.Bitangent);

    float3x3 TBN = float3x3(T, B, N);

    float3 tangentNormal = normalMapSampler.Sample(input.TexCoord).xyz * 2.0 - 1.0;

    return normalize(mul(tangentNormal, TBN));
}

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(perObject.model, float4(input.Pos, 1.0));
    output.FragPos_WorldSpace = worldPos.xyz;

    float3x3 normalMatrix = (float3x3)perObject.worldNormal;

    float3 N = normalize(mul(normalMatrix, input.Normal));
    float3 T = normalize(mul(normalMatrix, input.Tangent.xyz));

    T = normalize(T - N * dot(N, T));

    float3 B = cross(N, T) * input.Tangent.w;

    output.Normal = N;
    output.Tangent = T;
    output.Bitangent = B;

    output.TexCoord = input.TexCoord;

    output.Position = mul(scene.projection, mul(scene.view, worldPos));
    return output;
}

static const float RASTER_SHADOW_EPS = 0.002;

bool IsInShadow_RayQuery(float3 origin, float3 direction, float tMin, float tMax)
{
    RayDesc shadowRayDesc;
    shadowRayDesc.Origin = origin;
    shadowRayDesc.Direction = normalize(direction);
    shadowRayDesc.TMin = tMin;
    shadowRayDesc.TMax = tMax;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

    sq.TraceRayInline(accelerationStructure, rayFlags, 0xFF, shadowRayDesc);

    while (sq.Proceed())
    {
    }

    // If the shadow ray intersects an opaque triangle, we consider the pixel in shadow
    bool hit = (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    return hit;
}

//float Hammersley(int i, int N)
//{
//    uint bits = (uint(i) << 16) | (uint(i) >> 16);
//    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
//    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
//    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
//    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
//    return float(bits) * (1.0 / float(1 << 32));
//}

float2 Hammersley2D(int i, int N)
{
    uint bits = (uint(i) << 16) | (uint(i) >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);

    float u = (float(i) + 0.5) / float(N);
    float v = float(bits) * (1.0 / 4294967296.0);
    return float2(u, v);
}

// --- RTAO Helper Functions ---

[SpecializationConstant] const int rtaoSampleCount = 16;
static const float rtaoRadius = 3;

// Interleaved Gradient Noise for per-pixel randomness to avoid banding
float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189 * frac(dot(pixelPos, float2(0.06711056, 0.00583715))));
}

// Generate Tangent and Bitangent vectors from a Normal
void CreateONB(float3 n, out float3 t, out float3 b)
{
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// Map a 2D sequence point to a 3D cosine-weighted hemisphere direction
float3 CosineSampleHemisphere(float2 u, float angleOffset)
{
    float phi = 2.0 * PI * u.x + angleOffset;
    float cosTheta = sqrt(1.0 - u.y);
    float sinTheta = sqrt(u.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float CalculateRTAO(float3 worldPos, float3 N, float2 pixelPos)
{
    if (rtaoSampleCount <= 0) return 1.0;

    float3 t, b;
    CreateONB(N, t, b);

    // Get a random angle offset based on the screen pixel coordinate
    float noise = InterleavedGradientNoise(pixelPos);
    float angleOffset = 2.0 * PI * noise;

    float occlusionAcc = 0.0;

    for (int i = 0; i < rtaoSampleCount; i++) {
        // Get low-discrepancy 2D point
        float2 xi = Hammersley2D(i, rtaoSampleCount);

        // Convert to hemisphere vector
        float3 dirLocal = CosineSampleHemisphere(xi, angleOffset);

        // Transform local vector to world space aligned with the normal
        float3 dirWorld = normalize(t * dirLocal.x + b * dirLocal.y + N * dirLocal.z);

        if (IsInShadow_RayQuery(worldPos + N * RASTER_SHADOW_EPS, dirWorld, RASTER_SHADOW_EPS, rtaoRadius)) {
            occlusionAcc += 1.0;
        }
    }

    return 1.0 - (occlusionAcc / (float)rtaoSampleCount);
}

float CalculateVisibility(float3 lightDir, float3 worldPos, float3 N, float shadowTMax)
{
    float finalVisibility = 0.0;
    if (enableSoftShadows == 0)
    {
        float3 origin = worldPos + N * RASTER_SHADOW_EPS; // Offset to prevent self-intersection
        finalVisibility = IsInShadow_RayQuery(origin, lightDir, RASTER_SHADOW_EPS, shadowTMax) ? 0.0 : 1.0;
    }
    else
    {
        const int shadowSampleCount = 8;
        float visibilityAcc = 0.0;
        for (int i = 0; i < shadowSampleCount; i++) {
            float2 offset = Hammersley2D(i, shadowSampleCount) * 2.0 - 1.0; // Sample in [-1, 1] range
            float3 samplePos = worldPos + N * RASTER_SHADOW_EPS + (offset.x * N + offset.y * cross(N, lightDir)) * RASTER_SHADOW_EPS;
            if (!IsInShadow_RayQuery(samplePos, lightDir, RASTER_SHADOW_EPS, shadowTMax)) {
                visibilityAcc += 1.0;
            }
        }
        finalVisibility = visibilityAcc / (float)shadowSampleCount;
    }

    return finalVisibility;
}

LightResult CalculateSunlight(float3 worldPos, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightDir = normalize(globalLighting.sunLight.xyz);

    float visibility = CalculateVisibility(lightDir, worldPos, N, 1e4);

    LightResult sunResult = GetDirectLight(lightDir, V, N, F0, albedo, metallic, roughness);
    sunResult.diffuse = sunResult.diffuse * visibility;
    sunResult.specular = sunResult.specular * visibility;
    return sunResult;
}

LightResult CalculateLight(GPULight light, float3 worldPos, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightVec = light.position - worldPos;
    float distToLight = length(lightVec);
    float3 L = normalize(lightVec);

    float visibility = CalculateVisibility(L, worldPos, N, distToLight);

    float3 radiance = CalculateLightColor(light, worldPos);
    LightResult res = GetDirectLight(L, V, N, F0, albedo, metallic, roughness);
    res.diffuse = res.diffuse * radiance * visibility;
    res.specular = res.specular * radiance * visibility;
    return res;
}

[shader("fragment")]
PSOutput fragmentMain(VSOutput input)
{
    float3 N = CalculateNormal(input);
    float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);
    float3 R = reflect(-V, N);

    float metallicTex = metallicMapSampler.Sample(input.TexCoord).r;
    float metallic = clamp(metallicTex * perObject.material.metallic, 0.0, 1.0);
    float roughnessTex = roughnessMapSampler.Sample(input.TexCoord).r;
    float roughness = clamp(roughnessTex * perObject.material.roughness, 0.0, 1.0);

    float2 screenUV = input.Position.xy / scene.viewportSize;
    float globalAO = globalAoTexture.Sample(screenUV).r;
    float materialAO = aoMapSampler.Sample(input.TexCoord).r;
    float rtao = CalculateRTAO(input.FragPos_WorldSpace, N, input.Position.xy);
    float ao = rtao * materialAO;

    float3 albedoTex = albedoMapSampler.Sample(input.TexCoord).rgb;
    float3 albedo = albedoTex * perObject.material.albedo.rgb;

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);

    float3 totalDiffuse = float3(0.0);
    float3 totalSpecular = float3(0.0);

    LightResult sunResult = CalculateSunlight(input.FragPos_WorldSpace, N, V, F0, albedo, metallic, roughness);
    totalDiffuse += sunResult.diffuse;
    totalSpecular += sunResult.specular;

    for (int i = 0; i < scene.lightCount; i++) {
        GPULight light = sceneLights[i];

        LightResult lightResult = CalculateLight(light, input.FragPos_WorldSpace, N, V, F0, albedo, metallic, roughness);
        totalDiffuse += lightResult.diffuse;
        totalSpecular += lightResult.specular;
    }

    Lo = totalDiffuse + totalSpecular;

    float3 coloredAO = GTAOMultiBounce(ao, albedo);

    //float3 sampleN = float3(N.x, -N.y, N.z);
    //float3 sampleR = float3(R.x, -R.y, R.z);

    // IBL
    float2 brdf = samplerBRDFLUT.Sample(float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 reflection = prefilteredReflection(R, roughness).rgb;
    float3 irradiance = samplerIrradiance.Sample(N).rgb;

    // Diffuse indirect
    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 indirectDiffuse = irradiance * albedo * kD * coloredAO;

    // Specular indirect
    float specularOcclusion = GetSpecularOcclusion(max(dot(N, V), 0.0), ao, roughness);
    float3 indirectSpecular = reflection * (F0 * brdf.x + brdf.y) * specularOcclusion;

    float3 ambient = indirectDiffuse + indirectSpecular;

    float3 color = ambient + Lo;

    float3 emissiveTex = emissiveMapSampler.Sample(input.TexCoord).rgb;
    float3 emissive = emissiveTex * perObject.material.emissive;
    color += emissive;

    PSOutput output;
    output.Color = float4(color.xyz, 1.0);
    output.EntityID = perObject.entityID;
    return output;
}
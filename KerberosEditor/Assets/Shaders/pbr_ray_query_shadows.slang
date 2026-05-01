import common;
import lights;

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

[SpecializationConstant] const int enableSoftShadows = 0;

static const float PI = 3.14159265359;

// From http://filmicgames.com/archives/75
float3 Uncharted2Tonemap(float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Normal Distribution function --------------------------------------
float D_GGX(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
	return (alpha2)/(PI * denom*denom);
}

// Geometric Shadowing function --------------------------------------
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;
	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

// Fresnel function ----------------------------------------------------
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
float3 F_SchlickR(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Specular BRDF composition --------------------------------------------

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

float3 SpecularContribution(float3 L, float3 V, float3 N, float3 F0, float3 albedo, float metallic, float roughness)
{
    // Precalculate vectors and dot products
    float3 H = normalize(V + L);
    float dotNH = clamp(dot(N, H), 0.0, 1.0);
    float dotNV = clamp(dot(N, V), 0.0, 1.0);
    float dotNL = clamp(dot(N, L), 0.0, 1.0);

    // Light color fixed
    float3 lightColor = float3(1.0, 1.0, 1.0);

    float3 color = float3(0.0, 0.0, 0.0);

    if (dotNL > 0.0) {
        // D = Normal distribution (Distribution of the microfacets)
        float D = D_GGX(dotNH, roughness);
        // G = Geometric shadowing term (Microfacets shadowing)
        float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
        // F = Fresnel factor (Reflectance depending on angle of incidence)
        float3 F = F_Schlick(dotNV, F0);
        float3 spec = D * F * G / (4.0 * dotNL * dotNV + 0.001);
        float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
        color += (kD * albedo / PI + spec) * dotNL;
    }

    return color;
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

float3 CalculateSunlight(float3 worldPos, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightDir = normalize(globalLighting.sunLight.xyz);
    // TODO: Pass the sun color as parameter

    float visibility = CalculateVisibility(lightDir, worldPos, N, 1e4);
    float3 contribution = SpecularContribution(lightDir, V, N, F0, albedo, metallic, roughness);

    return contribution * visibility;
}

float3 CalculateLight(GPULight light, float3 worldPos, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightVec = light.position - worldPos;
    float distToLight = length(lightVec);
    float3 L = normalize(lightVec);

    float visibility = CalculateVisibility(L, worldPos, N, distToLight);

    float3 contrib = SpecularContribution(L, V, N, F0, albedo, metallic, roughness);
    float3 radiance = CalculateLightColor(light, worldPos);

    return contrib * radiance * visibility;
}

[shader("fragment")]
PSOutput fragmentMain(VSOutput input)
{
    float3 N = CalculateNormal(input);
    float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);
    float3 R = reflect(-V, N);

    // float metallic = perObject.material.metallic;
    // float roughness = perObject.material.roughness;
    float metallicTex = metallicMapSampler.Sample(input.TexCoord).r;
    float metallic = clamp(metallicTex * perObject.material.metallic, 0.0, 1.0);
    float roughnessTex = roughnessMapSampler.Sample(input.TexCoord).r;
    float roughness = clamp(roughnessTex * perObject.material.roughness, 0.0, 1.0);
    float ao = aoMapSampler.Sample(input.TexCoord).r;

    // Use this when working with srgb textures
    // float3 albedo = pow(albedoMapSampler.Sample(input.TexCoord).rgb * perObject.material.albedo, float3(2.2, 2.2, 2.2));
    float3 albedoTex = albedoMapSampler.Sample(input.TexCoord).rgb;
    float3 albedo = albedoTex * perObject.material.albedo.rgb;

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);

    Lo += CalculateSunlight(input.FragPos_WorldSpace, N, V, F0, albedo, metallic, roughness);

    for (int i = 0; i < scene.lightCount; i++) {
        GPULight light = sceneLights[i];

        Lo += CalculateLight(light, input.FragPos_WorldSpace, N, V, F0, albedo, metallic, roughness);
    }

    //float3 sampleN = float3(N.x, -N.y, N.z);
    //float3 sampleR = float3(R.x, -R.y, R.z);

    float2 brdf = samplerBRDFLUT.Sample(float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 reflection = prefilteredReflection(R, roughness).rgb;
    float3 irradiance = samplerIrradiance.Sample(N).rgb;

    // Diffuse based on irradiance
    float3 diffuse = irradiance * albedo;

    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);

    // Specular reflectance
    float3 specular = reflection * (F * brdf.x + brdf.y);

    // Ambient part
    float3 kD = 1.0 - F;
    kD *= 1.0 - perObject.material.metallic;
    float3 ambient = (kD * diffuse + specular) * ao;

    float3 color = ambient + Lo;

    //// Tone mapping
    //color = Uncharted2Tonemap(color * globalLighting.exposure);
    //color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
    //// Gamma correction
    //color = pow(color, (1.0f / globalLighting.gamma).xxx);

    PSOutput output;
    output.Color = float4(color.xyz, 1.0);
    output.EntityID = perObject.entityID;
    return output;
}
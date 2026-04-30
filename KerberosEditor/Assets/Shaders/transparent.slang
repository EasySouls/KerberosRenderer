import common;
import lights;
import pbr_helpers;

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
    float ViewSpaceZ;
};

struct PBOITData 
{
    float4 accumulation :   SV_Target0;
    float4 revealage :      SV_Target1;
    float2 distortion :     SV_Target2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(1, 0)]]
ConstantBuffer<GlobalLighting> globalLighting;

[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(3, 0)]]
Sampler2DArrayShadow shadowMap;

[[vk::binding(4, 0)]]
SamplerCube samplerIrradiance;
[[vk::binding(5, 0)]]
Sampler2D samplerBRDFLUT;
[[vk::binding(6, 0)]]
SamplerCube prefilteredMapSampler;

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

float CalculateWeight(float z, float alpha) {
    return clamp(pow(alpha + 0.01f, 2.0f) * 1e3 * exp(-z * z * 0.1f), 1e-2, 3e3);
}

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

float ShadowCalculation(float3 lightDir, float3 normal, float3 fragPosWorldSpace, float viewSpaceZ)
{
    // Determine which cascade to use based on View Space Z
    float depth = abs(viewSpaceZ);
    uint cascadeIndex = 0;

    for (uint i = 0; i < 3; ++i)
    {
        if (depth > scene.cascadeSplits[i])
        {
            cascadeIndex = i + 1;
        }
    }

    uint width, height, elements;
    shadowMap.GetDimensions(width, height, elements);
    float2 texelSize = 1.0 / float2(width, height);

    // Normal offset bias
    float3 shadowOffset = normal * (texelSize.x * 5.0);

    // Transform the world position to light space using the correct cascade matrix
    float4 fragPosLightSpace = mul(scene.lightSpaceMatrices[cascadeIndex], float4(fragPosWorldSpace + shadowOffset, 1.0));

    // Perform perspective divide
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;

    // Keep the shadow at 0.0 when outside the frustum bounds
    // if (projCoords.z > 1.0 || shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
    //    return 0.0;

    const float bias = 0.0001;
    const float currentDepth = projCoords.z - bias;
    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offsetUV = shadowUV + float2(x, y) * texelSize;

            // float pcfDepth = shadowMap.Sample(float3(offsetUV, cascadeIndex)).r;
            // shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
            shadow += shadowMap.SampleCmp(float3(offsetUV, cascadeIndex), currentDepth);
        }
    }
    shadow /= 9.0;

    return shadow;
}

float3 CalculateNormal(VSOutput input)
{
    float3 T = normalize(input.Tangent);
    float3 B = normalize(input.Bitangent);
    float3 N = normalize(input.Normal);

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

    output.ViewSpaceZ = mul(scene.view, worldPos).z;

    output.Position = mul(scene.projection, mul(scene.view, worldPos));
    return output;
}

[shader("fragment")]
PBOITData fragmentMain(VSOutput input)
{
    float3 N = CalculateNormal(input);
    float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);
    float3 R = reflect(-V, N);

    float metallicTex = metallicMapSampler.Sample(input.TexCoord).r;
    float metallic = clamp(metallicTex * perObject.material.metallic, 0.0, 1.0);
    float roughnessTex = roughnessMapSampler.Sample(input.TexCoord).r;
    float roughness = clamp(roughnessTex * perObject.material.roughness, 0.0, 1.0);
    float ao = aoMapSampler.Sample(input.TexCoord).r;
    float4 albedoTex = albedoMapSampler.Sample(input.TexCoord);
    float3 albedo = albedoTex.rgb * perObject.material.albedo.rgb;

    float baseAlpha = albedoTex.a * perObject.material.albedo.a;

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 totalDiffuse = float3(0.0);
    float3 totalSpecular = float3(0.0);

    float3 lightDir = normalize(globalLighting.sunLight.xyz);
    float shadow = ShadowCalculation(lightDir, N, input.FragPos_WorldSpace, input.ViewSpaceZ);
    LightResult sunResult = GetDirectLight(lightDir, V, N, F0, albedo, metallic, roughness);
    totalDiffuse += sunResult.diffuse * (1.0 - shadow);
    totalSpecular += sunResult.specular * (1.0 - shadow);

    for (int i = 0; i < scene.lightCount; i++) {
        GPULight light = sceneLights[i];
        float3 lVec = light.position - input.FragPos_WorldSpace;
        float3 L = normalize(lVec);
        float3 radiance = CalculateLightColor(light, input.FragPos_WorldSpace);

        LightResult lightResult = GetDirectLight(L, V, N, F0, albedo, metallic, roughness);
        totalDiffuse += lightResult.diffuse * radiance;
        totalSpecular += lightResult.specular * radiance;
    }

    float2 brdf = samplerBRDFLUT.Sample(float2(max(dot(N, V), 0.0), roughness)).rg;
    float3 reflection = prefilteredReflection(R, roughness).rgb;
    float3 irradiance = samplerIrradiance.Sample(N).rgb;
    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, roughness);

    float3 kD = (1.0 - F) * (1.0 - metallic);
    totalDiffuse += (kD * irradiance * albedo) * ao;
    totalSpecular += (reflection * (F * brdf.x + brdf.y)) * ao;

    float3 finalColor = (totalDiffuse * baseAlpha) + totalSpecular;

    float maxSpec = max(totalSpecular.r, max(totalSpecular.g, totalSpecular.b));
    float effectiveAlpha = clamp(baseAlpha + maxSpec, 0.0, 1.0);
    
    float z = abs(input.ViewSpaceZ); 
    float weight = CalculateWeight(z, effectiveAlpha);

    float3 viewNormal = mul(scene.view, float4(N, 0.0f)).xyz;
    float ior = 1.52f; // Glass
    float scaleFactor = 0.05f;
    float distortionStrength = (ior - 1.0f) * scaleFactor;
    float2 uvOffset = viewNormal.xy * distortionStrength;
    
    PBOITData output;
    output.accumulation = float4(finalColor, effectiveAlpha) * weight;
    output.revealage = float4(effectiveAlpha.xxx, 1.0f);
    output.distortion = uvOffset * effectiveAlpha;
    
    return output;
}
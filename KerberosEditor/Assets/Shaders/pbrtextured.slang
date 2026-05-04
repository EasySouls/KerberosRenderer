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
    float ViewSpaceZ;
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
Sampler2DArrayShadow shadowMap;

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

[SpecializationConstant] const int enablePCF = 0;

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
    //if (projCoords.z > 1.0 || shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
    //    return 0.0;

    const float bias = 0.0001;
    const float currentDepth = projCoords.z - bias;
    float shadow = 0.0;

    if (enablePCF == 1) {
        for (int x = -1; x <= 1; ++x)
        {
            for (int y = -1; y <= 1; ++y)
            {
                float2 offsetUV = shadowUV + float2(x, y) * texelSize;

                //float pcfDepth = shadowMap.Sample(float3(offsetUV, cascadeIndex)).r;
                //shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
                shadow += shadowMap.SampleCmp(float3(offsetUV, cascadeIndex), currentDepth);
            }
        }
        shadow /= 9.0;
    } else {
        shadow = shadowMap.SampleCmp(float3(shadowUV, cascadeIndex), currentDepth);
        //float closestDepth = shadowMap.Sample(float3(shadowUV, cascadeIndex)).r;
        //shadow = currentDepth > closestDepth ? 1.0 : 0.0;
    }

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

LightResult CalculateSunlight(float3 fragPosWorldSpace, float viewSpaceZ, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightDir = normalize(globalLighting.sunLight.xyz);

    float shadow = ShadowCalculation(lightDir, N, fragPosWorldSpace, viewSpaceZ);

    LightResult sunResult = GetDirectLight(lightDir, V, N, F0, albedo, metallic, roughness);
    sunResult.diffuse = sunResult.diffuse * (1.0 - shadow);
    sunResult.specular = sunResult.specular * (1.0 - shadow);
    return sunResult;
}

LightResult CalculateLight(GPULight light, float3 worldPos, float3 N, float3 V, float3 F0, float3 albedo, float metallic, float roughness)
{
    float3 lightVec = light.position - worldPos;
    float3 L = normalize(lightVec);

    float3 radiance = CalculateLightColor(light, worldPos);
    LightResult res = GetDirectLight(L, V, N, F0, albedo, metallic, roughness);
    res.diffuse = res.diffuse * radiance;
    res.specular = res.specular * radiance;
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
    float ao = globalAO * materialAO;

    // Use this when working with srgb textures
    // float3 albedo = pow(albedoMapSampler.Sample(input.TexCoord).rgb * perObject.material.albedo, float3(2.2, 2.2, 2.2));
    float3 albedoTex = albedoMapSampler.Sample(input.TexCoord).rgb;
    float3 albedo = albedoTex * perObject.material.albedo.rgb;

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);

    float3 totalDiffuse = float3(0.0);
    float3 totalSpecular = float3(0.0);

    LightResult sunResult =  CalculateSunlight(input.FragPos_WorldSpace, input.ViewSpaceZ, N, V, F0, albedo, metallic, roughness);
    totalDiffuse += sunResult.diffuse;
    totalSpecular += sunResult.specular;

    for (int i = 0; i < scene.lightCount; i++) {
        GPULight light = sceneLights[i];

        LightResult lightResult = CalculateLight(light, input.FragPos_WorldSpace, N, V, F0, albedo, metallic, roughness);
        totalDiffuse += lightResult.diffuse;
        totalSpecular += lightResult.specular;
    }

    Lo = totalDiffuse + totalSpecular;

    //float3 sampleN = float3(N.x, -N.y, N.z);
    //float3 sampleR = float3(R.x, -R.y, R.z);

    float3 coloredAO = GTAOMultiBounce(ao, albedo);

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
    float3 indirectSpecular = reflection * (F * brdf.x + brdf.y) * specularOcclusion;

    float3 ambient = (kD * indirectDiffuse + indirectSpecular) * ao;

    float3 color = ambient + Lo;

    //// Tone mapping
    //color = Uncharted2Tonemap(color * globalLighting.exposure);
    //color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
    //// Gamma correction
    //color = pow(color, (1.0f / globalLighting.gamma).xxx);

    PSOutput output;
    output.Color = float4(color, 1.0);
    output.EntityID = perObject.entityID;
    return output;
}
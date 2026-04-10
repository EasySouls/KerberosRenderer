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
    float TangentW;  // glTF tangent handedness (+1 or -1)
    float2 TexCoord : TEXCOORD0;
    float4 FragPos_LightSpace;
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

[SpecializationConstant] const int enablePCF = 0;

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

float ShadowCalculation(float3 lightDir, float3 normal, float4 fragPosLightSpace)
{
    // Perform perspective divide
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;

    float closestDepth = shadowMap.Sample(shadowUV).r;
    float currentDepth = projCoords.z;

    // Hardware-based depth bias is enabled
    //float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    float shadow = 0.0;
    if (enablePCF == 1) {
        // Percentage-closer filtering (PCF)
        uint width, height;
        shadowMap.GetDimensions(width, height);
        float2 texelSize = 1.0 / float2(width, height);

        for (int x = -1; x <= 1; ++x)
        {
            for(int y = -1; y <= 1; ++y)
            {
                float pcfDepth = shadowMap.Sample(shadowUV + float2(x, y) * texelSize).r;
                //shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
                shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
            }
        }
        shadow /= 9.0;
    } else {
        //shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
        shadow = currentDepth > closestDepth ? 1.0 : 0.0;
    }

    // Keep the shadow at 0.0 when outside the far plane of the light's orthographic frustum.
    if (currentDepth > 1.0 || shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
        shadow = 0.0;

    return shadow;
}

float3 CalculateNormal(VSOutput input)
{
    float3 N = normalize(input.Normal);
    float3 T = input.Tangent;

    if (dot(T, T) < 1e-6)
        return N;

    // Re-orthogonalize (Gram-Schmidt)
    T = normalize(T - N * dot(N, T));

    float3 B = cross(N, T) * input.TangentW;

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
    output.Normal = mul(normalMatrix, input.Normal);
    output.Tangent = mul(normalMatrix, input.Tangent.xyz);
    output.TangentW = input.Tangent.w;

    output.TexCoord = input.TexCoord;

    output.FragPos_LightSpace = mul(scene.lightSpaceMatrix, float4(output.FragPos_WorldSpace, 1.0));

    output.Position = mul(scene.projection, mul(scene.view, worldPos));
    return output;
}

[shader("fragment")]
PSOutput fragmentMain(VSOutput input)
{
    float3 N = CalculateNormal(input);
    float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);
    float3 R = reflect(-V, N);

    //float metallic = perObject.material.metallic;  
    //float roughness = perObject.material.roughness; 
    float metallic = metallicMapSampler.Sample(input.TexCoord).r;
    float roughness = roughnessMapSampler.Sample(input.TexCoord).r;
    float ao = aoMapSampler.Sample(input.TexCoord).r;

    // Currently we are using light 0 only for generating the shadow map
    float3 lightDir = normalize(globalLighting.lights[0].xyz);
    float shadow = ShadowCalculation(lightDir, N, input.FragPos_LightSpace);

    float3 F0 = float3(0.04, 0.04, 0.04);
    // Use this when working with srgb textures
    //float3 albedo = pow(albedoMapSampler.Sample(input.TexCoord).rgb * perObject.material.albedo, float3(2.2, 2.2, 2.2));
    float3 albedo = albedoMapSampler.Sample(input.TexCoord).rgb * perObject.material.albedo;
    F0 = lerp(F0, albedo, metallic);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < 4; i++) {
        // For point lights in the future
        // float3 L = normalize(normalize(uboParams.lights[i].xyz) - input.FragPos_WorldSpace);
        float3 L = normalize(globalLighting.lights[i].xyz);

        float3 contribution = SpecularContribution(L, V, N, F0, albedo, metallic, roughness);
        if (i == 0) // Only apply shadow to the first light for now
            contribution *= (1.0 - shadow);

        Lo += contribution;
    };
    for (int i = 0; i < scene.lightCount; i++) {
        GPULight light = sceneLights[i];
        float3 lightVec = light.position - input.FragPos_WorldSpace;
        float3 L = normalize(lightVec);

        float3 contrib = SpecularContribution(L, V, N, F0, albedo, metallic, roughness);
        float3 radiance = CalculateLightColor(sceneLights[i], input.FragPos_WorldSpace);

        Lo += contrib * radiance;
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
    kD *= 1.0 - metallic;
    float3 ambient = (kD * diffuse + specular) * ao;

    float3 color = ambient + Lo;

    // Tone mapping
    color = Uncharted2Tonemap(color * globalLighting.exposure);
    color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
    // Gamma correction
    color = pow(color, (1.0f / globalLighting.gamma).xxx);

    PSOutput output;
    output.Color = float4(color, 1.0);
    output.EntityID = perObject.entityID;
    return output;
}
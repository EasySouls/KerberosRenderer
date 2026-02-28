struct VSInput
{
    float3 Pos;
    float3 Normal;
    float2 TexCoord;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 FragPos_WorldSpace;
    float3 Normal_WorldSpace;
    float2 TexCoord : TEXCOORD0;
    float4 FragPos_LightSpace;
};

struct SceneData
{
    float4x4 projection;
    float4x4 view;
    float4x4 lightSpaceMatrix;
    float3 ambientLightColor;
    float3 camPos;
};
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

struct UBOParams {
	float4 lights[4];
    float exposure;
    float gamma;
};
[[vk::binding(1, 0)]]
ConstantBuffer<UBOParams> uboParams;

struct Material {
    float3 albedo;
	float roughness;
	float metallic;
};

struct PerObject {
    float4x4 model;
    float4x4 worldNormal;
    Material material;
};
[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(3, 0)]]
Sampler2D shadowMap;

[[vk::binding(4, 0)]]
SamplerCube samplerIrradiance;
[[vk::binding(5, 0)]]
Sampler2D samplerBRDFLUT;
[[vk::binding(6, 0)]]
SamplerCube prefilteredMapSampler;

[[vk::binding(0, 1)]]
Sampler2D albedoMapSampler;
[[vk::binding(1, 1)]]
Sampler2D normalMapSampler;
//[[vk::binding(2, 1)]]
//Sampler2D aoMapSampler;
//[[vk::binding(3, 1)]]
//Sampler2D metallicMapSampler;
//[[vk::binding(4, 1)]]
//Sampler2D roughnessMapSampler;

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

float3 SpecularContribution(float3 L, float3 V, float3 N, float3 F0, Material material)
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
        float D = D_GGX(dotNH, material.roughness);
        // G = Geometric shadowing term (Microfacets shadowing)
        float G = G_SchlicksmithGGX(dotNL, dotNV, material.roughness);
        // F = Fresnel factor (Reflectance depending on angle of incidence)
        float3 F = F_Schlick(dotNV, F0);
        float3 spec = D * F * G / (4.0 * dotNL * dotNV + 0.001);
        float3 kD = (float3(1.0, 1.0, 1.0) - F) * (1.0 - material.metallic);
        color += (kD * material.albedo / PI + spec) * dotNL;
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

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(perObject.model, float4(input.Pos, 1.0));
    output.FragPos_WorldSpace = worldPos.xyz;

    float3x3 normalMatrix = (float3x3)perObject.worldNormal;
    output.Normal_WorldSpace = mul(normalMatrix, input.Normal);

    output.TexCoord = input.TexCoord;

    output.FragPos_LightSpace = mul(scene.lightSpaceMatrix, float4(output.FragPos_WorldSpace, 1.0));

    output.Position = mul(scene.projection, mul(scene.view, worldPos));
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input)
{
	float3 N = normalize(input.Normal_WorldSpace);
    float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);
    float3 R = reflect(-V, N);

    // For point lights in the future
    // float3 lightDir = normalize(uboParams.lights[0].xyz - input.FragPos_WorldSpace);

    // Currently we are using light 0 only for generating the shadow map
    float3 lightDir = normalize(uboParams.lights[0].xyz);
    float shadow = ShadowCalculation(lightDir, N, input.FragPos_LightSpace);

    float3 F0 = float3(0.04, 0.04, 0.04);
    float3 albedo = pow(albedoMapSampler.Sample(input.TexCoord).rgb, float3(2.2, 2.2, 2.2));
    F0 = lerp(F0, albedo, perObject.material.metallic);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < 4; i++) {
        // For point lights in the future
        // float3 L = normalize(normalize(uboParams.lights[i].xyz) - input.FragPos_WorldSpace);
        float3 L = normalize(uboParams.lights[i].xyz);

        float3 contribution = SpecularContribution(L, V, N, F0, perObject.material);
        if (i == 0) // Only apply shadow to the first light for now
            contribution *= (1.0 - shadow);

        Lo += contribution;
	};

    float3 sampleN = float3(N.x, -N.y, N.z);
    float3 sampleR = float3(R.x, -R.y, R.z);

    float2 brdf = samplerBRDFLUT.Sample(float2(max(dot(N, V), 0.0), perObject.material.roughness)).rg;
    float3 reflection = prefilteredReflection(sampleR, perObject.material.roughness).rgb;
    float3 irradiance = samplerIrradiance.Sample(sampleN).rgb;

    // Diffuse based on irradiance
    float3 diffuse = irradiance * albedo;

    float3 F = F_SchlickR(max(dot(N, V), 0.0), F0, perObject.material.roughness);

    // Specular reflectance
    float3 specular = reflection * (F * brdf.x + brdf.y);

    // Ambient part
    float3 kD = 1.0 - F;
    kD *= 1.0 - perObject.material.metallic;
    float3 ambient = (kD * diffuse + specular);

    float3 color = ambient + Lo;

    // Tone mapping
    color = Uncharted2Tonemap(color * uboParams.exposure);
    color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
    // Gamma correction
    color = pow(color, (1.0f / uboParams.gamma).xxx);

    return float4(color, 1.0);
}
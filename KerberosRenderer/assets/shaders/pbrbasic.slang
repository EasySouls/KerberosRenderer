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
[[vk::binding(0)]]
ConstantBuffer<SceneData> scene;

struct UBOParams {
	float4 lights[4];
    float exposure;
    float gamma;
};
[[vk::binding(1)]]
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
[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(3)]]
Sampler2D shadowMap;

static const float PI = 3.14159265359;

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
float3 F_Schlick(float cosTheta, Material material)
{
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), material.albedo, material.metallic); // * material.specular
	float3 F = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
	return F;
}

// Specular BRDF composition --------------------------------------------

float3 BRDF(float3 L, float3 V, float3 N, Material material)
{
	// Precalculate vectors and dot products
	float3 H = normalize (V + L);
	float dotNV = clamp(dot(N, V), 0.0, 1.0);
	float dotNL = clamp(dot(N, L), 0.0, 1.0);
	float dotLH = clamp(dot(L, H), 0.0, 1.0);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);

	// Light color fixed
	float3 lightColor = float3(1.0, 1.0, 1.0);

	float3 color = float3(0.0, 0.0, 0.0);

	if (dotNL > 0.0)
    {
        float rroughness = max(0.05, material.roughness);
        // D = Normal distribution (Distribution of the microfacets)
        float D = D_GGX(dotNH, material.roughness);
		// G = Geometric shadowing term (Microfacets shadowing)
		float G = G_SchlicksmithGGX(dotNL, dotNV, rroughness);
        // F = Fresnel factor (Reflectance depending on angle of incidence)
        float3 F = F_Schlick(dotNV, material);

		float3 spec = D * F * G / (4.0 * dotNL * dotNV);

		color += spec * dotNL * lightColor;
	}

	return color;
}

[shader("vertex")]
VSOutput vertMain(VSInput input)
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
float4 fragMain(VSOutput input)
{
	float3 N = normalize(input.Normal_WorldSpace);
	float3 V = normalize(scene.camPos - input.FragPos_WorldSpace);

	// Specular contribution
	float3 Lo = float3(0.0, 0.0, 0.0);
	for (int i = 0; i < 4; i++) {
		float3 L = normalize(normalize(uboParams.lights[i].xyz) - input.FragPos_WorldSpace);
		Lo += BRDF(L, V, N, perObject.material);
	};

    // Ambient background lighting
    float3 ambient = scene.ambientLightColor * perObject.material.albedo;

    // Combine with ambient
    float3 color = (ambient + Lo) * uboParams.exposure;
    //float3 color = perObject.material.albedo;
    //float3 color = float3(0.5, 0.2, 0.8);

    // Gamma correct
    color = pow(color, (1.0f / uboParams.gamma).xxx);

    return float4(color, 1.0);
}
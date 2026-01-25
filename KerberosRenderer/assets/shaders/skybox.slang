/* Copyright (c) 2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct VSInput
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 UVW;
};

struct SceneData
{
    float4x4 projection;
    float4x4 view;
    float4x4 viewProjection;
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
    float3 objPos;
    float4x4 model;
    float4x4 worldNormal;
    Material material;
};
[[vk::binding(2)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(4)]]
SamplerCube samplerEnv;

// From http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 Uncharted2Tonemap(float3 color)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((color*(A*color+C*B)+D*E)/(color*(A*color+B)+D*F))-E/F;
}

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.UVW = input.Position;
    output.Pos = mul(scene.projection, mul(perObject.model, float4(input.Position.xyz, 1.0)));
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input)
{
    float3 color = samplerEnv.Sample(input.UVW).rgb;

	// Tone mapping
	color = Uncharted2Tonemap(color * uboParams.exposure);
	color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
	// Gamma correction
	color = pow(color, (1.0f / uboParams.gamma).xxx);

	return float4(color, 1.0);
}
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

struct SkyboxData
{
	float4x4 projection;
	float4x4 model;
};

[[vk::binding(0)]]
ConstantBuffer<SkyboxData> skyboxData;

struct UBOParams {
    float4 lights[4];
	float exposure;
	float gamma;
};
[[vk::binding(1)]]
ConstantBuffer<UBOParams> uboParams;

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
    output.Pos = mul(skyboxData.projection, mul(skyboxData.model, float4(input.Position.xyz, 1.0)));
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input)
{
    float3 sampleUV = float3(input.UVW.x, -input.UVW.y, input.UVW.z);
    float3 color = samplerEnv.Sample(sampleUV).rgb;

	// Tone mapping
	color = Uncharted2Tonemap(color * uboParams.exposure);
	color = color * (1.0f / Uncharted2Tonemap((11.2f).xxx));
	// Gamma correction
	color = pow(color, (1.0f / uboParams.gamma).xxx);

	return float4(color, 1.0);
}
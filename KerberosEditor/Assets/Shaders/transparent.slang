import common;

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

struct PBOITData {
    float4 accumulation :   SV_Target0;
    float4 revealage :      SV_Target1;
    float2 distortion :     SV_Target2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[[vk::binding(2, 0)]]
ConstantBuffer<PerObject> perObject;

[[vk::binding(0, 1)]]
Sampler2D albedoMapSampler;

float CalculateWeight(float z, float alpha) {
    return clamp(pow(alpha + 0.01f, 2.0f) * 1e3 * exp(-z * z * 0.1f), 1e-2, 3e3);
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
    float4 color = albedoMapSampler.Sample(input.TexCoord);
    float alpha = color.a * perObject.material.albedo.a;
    
    float z = abs(input.ViewSpaceZ); 
    
    float weight = CalculateWeight(z, alpha);

    float3 viewNormal = mul(scene.view, float4(input.Normal, 0.0f)).xyz;
    float ior = 1.52f; // Glass
    float scaleFactor = 0.05f;
    float distortionStrength = (ior - 1.0f) * scaleFactor;

    float2 uvOffset = viewNormal.xy * distortionStrength;
    
    PBOITData output;
    output.accumulation = float4(color.rgb * alpha, alpha) * weight;
    output.revealage = alpha;
    output.distortion = uvOffset * alpha;
    
    return output;
}
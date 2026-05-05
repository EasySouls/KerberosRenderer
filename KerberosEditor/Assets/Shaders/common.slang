module common;

public struct SceneData
{
    public float4x4 projection;
    public float4x4 view;
    public float4x4 lightSpaceMatrices[4];
    public float4 cascadeSplits;
    public float3 camPos;
    public uint lightCount;
    public float2 viewportSize;
};

public struct Material 
{
    public float4 albedo;
    public float3 emissive;
    public float roughness;
    public float metallic;
};

public struct PerObject 
{
    public float4x4 model;
    public float4x4 worldNormal;
    public Material material;
    public uint entityID;
    private float3 _Padding;
};

public struct GlobalLighting
{
    public float4 sunLight;
    public float exposure;
    public float gamma;
};
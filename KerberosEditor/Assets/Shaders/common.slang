module common;

public struct SceneData
{
    public float4x4 projection;
    public float4x4 view;
    public float4x4 lightSpaceMatrix;
    public float3 ambientLightColor;
    public float3 camPos;
    public uint lightCount;
};

public struct Material 
{
    public float3 albedo;
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
    public float4 lights[4];
    public float exposure;
    public float gamma;
};
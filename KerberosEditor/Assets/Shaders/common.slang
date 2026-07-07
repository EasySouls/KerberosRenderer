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
    public float3 cameraRight;
    public float deltaTime;
    public float3 cameraUp;
    public float time;
};

public struct Material 
{
    public float4 albedo;
    public float3 emissive;
    public float roughness;
    public float metallic;

    public uint albedoTexIndex;
    public uint normalTexIndex;
    public uint roughnessTexIndex;
    public uint metallicTexIndex;
    public uint aoTexIndex;
    public uint emissiveTexIndex;

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

public struct DrawIndirectCommand 
{
    public uint vertexCount;
    public uint instanceCount;
    public uint firstVertex;
    public uint firstInstance;
};
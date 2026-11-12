module common;

public struct SceneData
{
    public float4x4 projection;
    public float4x4 view;
    public float4x4 previousProjection;
    public float4x4 previousView;
    public float4x4 lightSpaceMatrices[4];
    public float4 cascadeSplits;
    public float3 camPos;
    public uint lightCount;
    public float2 viewportSize;
    public float3 cameraRight;
    public float deltaTime;
    public float3 cameraUp;
    public float time;
    public float4x4 motionProjection; // Unjittered projection for motion vectors
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
    public float4x4 previousModel;
    public float4x4 worldNormal;
    public Material material;
    // 0 = opaque, 1 = mask, 2 = blend.
    public uint alphaMode;
    public float alphaCutoff;
    public uint entityID;
    float3 _Padding2;
};

// Shared alpha-mask test.
// OPAQUE ignores alpha entirely
// MASK discards below the cutoff
// BLEND is handled by the weighted-blended OIT pass.
public bool ShouldDiscardAlpha(float alpha, uint mode, float cutoff)
{
    return mode == 1u && alpha < cutoff;
}

public struct GlobalLighting
{
    // xyz = normalized sun direction, w = 1 when a sun is active.
    public float4 sunDirection;
    // rgb = linear sun color, w = sun illuminance in lux.
    public float4 sunColor;
    // x = exposure scale, y = environment intensity, z/w = reserved.
    public float4 lightingParams;
};

public struct DrawIndirectCommand 
{
    public uint vertexCount;
    public uint instanceCount;
    public uint firstVertex;
    public uint firstInstance;
};
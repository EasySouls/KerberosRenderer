import particles;
import common;

[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> scene;

[vk::binding(0, 1)] StructuredBuffer<Particle> particlePool;
[vk::binding(2, 1)] StructuredBuffer<uint> aliveList;

[vk::binding(0, 3)] SamplerState particleSampler;
[vk::binding(1, 3)] Texture2D particleTexture;

struct VertexOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

static const float2 QUAD_POSITIONS[6] = {
    float2(-0.5, -0.5), float2( 0.5, -0.5), float2(-0.5,  0.5),
    float2( 0.5, -0.5), float2( 0.5,  0.5), float2(-0.5,  0.5)
};
static const float2 QUAD_UVS[6] = {
    float2(0.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0),
    float2(1.0, 1.0), float2(1.0, 0.0), float2(0.0, 0.0)
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexID: SV_VertexID, uint instanceID: SV_InstanceID) {
    VertexOutput output;

    uint poolIndex = aliveList[instanceID];
    Particle p = particlePool[poolIndex];

    float2 quadPos = QUAD_POSITIONS[vertexID] * p.size;
    float3 worldPos = p.position 
                    + (scene.cameraRight * quadPos.x) 
                    + (scene.cameraUp * quadPos.y);

    output.position = mul(scene.projection, mul(scene.view, float4(worldPos, 1.0)));
    output.color = p.color;
    output.uv = QUAD_UVS[vertexID];

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    // float4 texColor = particleTexture.Sample(particleSampler, input.uv);
    //float4 finalColor = texColor * input.color;
    float4 finalColor = input.color;

    // Alpha clipping
    // if (finalColor.a < 0.01) discard;

    return finalColor;
}
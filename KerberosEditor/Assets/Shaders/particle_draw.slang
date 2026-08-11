import particles;

[vk::binding(0, 0)] StructuredBuffer<Particle> particlePool;
[vk::binding(2, 0)] StructuredBuffer<uint> aliveList;

[vk::binding(1, 1)] ConstantBuffer<ParticleFrameData> scene;

[vk::binding(0, 2)] Texture2D particleTexture;
[vk::binding(1, 2)] SamplerState particleSampler;

struct VertexOutput {
    float4 position     : SV_Position;
    float4 color        : COLOR;
    float2 uv1          : TEXCOORD0; // UV for current frame
    float2 uv2          : TEXCOORD1; // UV for next frame
    float blendFactor   : BLEND;     // Transition between frames
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
VertexOutput vertexMain(uint vertexID: SV_VulkanVertexID, uint instanceID: SV_VulkanInstanceID) {
    VertexOutput output;

    uint poolIndex = aliveList[instanceID];
    Particle p = particlePool[poolIndex];

    float age = 1.0 - (p.life / p.maxLife);

    uint columns = (uint)max(1.0, p.subUVGrid.x);
    uint rows    = (uint)max(1.0, p.subUVGrid.y);
    uint totalFrames = columns * rows;

    float frameProgress = age * (float)totalFrames;
    frameProgress = clamp(frameProgress, 0.0, (float)totalFrames - 1.0001);

    uint frame1 = (uint)floor(frameProgress);
    uint frame2 = min(frame1 + 1, totalFrames - 1);
    float blend = frac(frameProgress);

    float2 uvSize = float2(1.0 / (float)columns, 1.0 / (float)rows);

    float2 offset1 = float2((float)(frame1 % columns), (float)(frame1 / columns)) * uvSize;
    float2 offset2 = float2((float)(frame2 % columns), (float)(frame2 / columns)) * uvSize;

    float2 baseUV = QUAD_UVS[vertexID];
    output.uv1 = baseUV * uvSize + offset1;
    output.uv2 = baseUV * uvSize + offset2;
    output.blendFactor = blend;

    float2 quadPos = QUAD_POSITIONS[vertexID] * p.size;

    float3 worldPos = p.position 
                    + (scene.cameraRight * quadPos.x) 
                    + (scene.cameraUp * quadPos.y);

    output.position = mul(scene.viewProj, float4(worldPos, 1.0));
    output.color = p.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float4 texColor1 = particleTexture.Sample(particleSampler, input.uv1);
    float4 texColor2 = particleTexture.Sample(particleSampler, input.uv2);
    float4 blendedTexColor = lerp(texColor1, texColor2, input.blendFactor);

    float4 finalColor = blendedTexColor * input.color;
    // Premultiply alpha for correct blending
    finalColor.rgb *= finalColor.a;
    //float4 finalColor = input.color;

    if (finalColor.a < 0.01) discard;

    // The 0 alpha is important, because the alpha of every pass before the bloom pass
    // is considered to be the bloom mask, and we don't want to bloom the particles.
    return float4(finalColor.xyz, 0);
}
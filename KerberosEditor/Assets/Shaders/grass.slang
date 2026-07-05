public struct VertexOut {
    public float4 position : SV_Position; 
    public float3 normal   : NORMAL;
    public float2 uv       : TEXCOORD0;
};

public struct GrassPayload {
    public uint chunkID;
    public float3 chunkCenter;
    public uint lodLevel; // 0 = high detail, 1 = low detail
};

struct GrassConstants {
    float4x4 ViewProj;
    float time;
};

[[vk::push_constant]]
GrassConstants Constants;

StructuredBuffer<float4> ChunkBounds;

bool isInsideFrustum(float4 bounds) {
    // TODO: Check the bounds against the camera's frustum planes
    return true;
}

groupshared GrassPayload payload;

[shader("amplification")]
[numthreads(32, 1, 1)] // 32 chunks evaluated per workgroup
void taskMain(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint localID: SV_GroupIndex
) {
    uint chunkID = dispatchThreadID.x;

    float4 bounds = ChunkBounds[chunkID];

    bool isVisible = isInsideFrustum(bounds);

    if (localID == 0) {
        payload.chunkID = chunkID;
        payload.chunkCenter = bounds.xyz;
        payload.lodLevel = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    uint dispatchX = isVisible ? 1 : 0;
    DispatchMesh(dispatchX, 1, 1, payload);
}

static const uint MAX_VERTICES = 256;
static const uint MAX_PRIMITIVES = 128;

float3 calculateBladeOffset(uint localID) {
    // Simple offset based on localID to spread blades within a chunk
    float angle = (localID / 64.0) * 6.28318530718; // 2 * PI
    float radius = 0.5;                             // Spread radius
    return float3(cos(angle) * radius, 0, sin(angle) * radius);
}

float3 calculateWind(float3 position) {
    // Simple wind effect based on time and position
    float windStrength = 0.1;
    float windFrequency = 1.0;
    return float3(sin(Constants.time * windFrequency + position.x) * windStrength, 0, 0);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    in payload GrassPayload payload,
    uint localID: SV_GroupIndex,
    OutputVertices<VertexOut, MAX_VERTICES> verts,
    OutputIndices<uint3, MAX_PRIMITIVES> tris
) {
    // 1. Tell the GPU how much geometry this specific workgroup will output.
    // Must be called once before writing to verts/tris!
    // E.g., 64 blades * 3 vertices per blade = 192 vertices, 64 triangles.
    SetMeshOutputCounts(192, 64);

    // 2. Procedurally generate the grass blade for this specific thread
    float3 rootPos = payload.chunkCenter + calculateBladeOffset(localID);

    // Add wind bending based on root position and time
    float3 windOffset = calculateWind(rootPos);

    // 3. Write Vertices
    uint vOffset = localID * 3;

    VertexOut v0, v1, v2;
    v0.position = mul(Constants.ViewProj, float4(rootPos + float3(-0.1, 0, 0), 1.0));
    v1.position = mul(Constants.ViewProj, float4(rootPos + float3(0.1, 0, 0), 1.0));
    // The tip of the blade moves with the wind
    v2.position = mul(Constants.ViewProj, float4(rootPos + float3(0, 1.0, 0) + windOffset, 1.0));

    v0.uv = float2(0, 0); v0.normal = float3(0, 0, 1);
    v1.uv = float2(1, 0); v1.normal = float3(0, 0, 1);
    v2.uv = float2(0.5, 1); v2.normal = float3(0, 0, 1);

    verts[vOffset + 0] = v0;
    verts[vOffset + 1] = v1;
    verts[vOffset + 2] = v2;

    // 4. Write Indices (connecting the vertices into a triangle)
    uint tOffset = localID;
    tris[tOffset] = uint3(vOffset + 0, vOffset + 1, vOffset + 2);
}

struct FragmentOut {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOut fragmentMain(VertexOut input) {
    FragmentOut output;

    // You now have access to the interpolated normal and UV!
    float3 lightDir = normalize(float3(0.5, -1.0, 0.5));
    float ndotl = max(dot(input.normal, -lightDir), 0.2);

    // Sample a grass texture or just use color math
    float3 grassColor = float3(0.1, 0.8, 0.2) * ndotl;

    output.color = float4(grassColor, 1.0);
    return output;
}
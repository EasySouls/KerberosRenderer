struct InputVertex {
    float p[3];
    float n[3];
    float t[4];
    float uv[2];
};

struct OutputVertex {
    float p[3];
    float n[3];
    float t[4];
    float uv[2];
};

// Joint data is stored in two separate buffers rather than being embedded in
// InputVertex, so the rest-pose geometry buffer never needs to change.
[[vk::binding(0, 0)]] StructuredBuffer<InputVertex>    inputVertices;
[[vk::binding(1, 0)]] RWStructuredBuffer<OutputVertex> outputVertices;
[[vk::binding(2, 0)]] StructuredBuffer<float4x4>       jointMatrices;
[[vk::binding(3, 0)]] StructuredBuffer<uint4>          jointIndices;
[[vk::binding(4, 0)]] StructuredBuffer<float4>         jointWeights;

struct SkinPushConstants {
    uint vertexCount;
};
[[vk::push_constant]] SkinPushConstants pushConstants;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint vertexId = dispatchThreadID.x;
    if (vertexId >= pushConstants.vertexCount) 
        return;

    InputVertex v = inputVertices[vertexId];
    uint4  j_idx  = jointIndices[vertexId];
    float4 j_w    = jointWeights[vertexId];

    // Build the blended skin matrix from the four joint influences.
    // This is the core of the Linear Blend Skinning (LBS) equation:
    // SkinMatrix = sum(weight_i * JointMatrix_i) for i in [0, 3]
    float4x4 skinMatrix =
        j_w.x * jointMatrices[j_idx.x] +
        j_w.y * jointMatrices[j_idx.y] +
        j_w.z * jointMatrices[j_idx.z] +
        j_w.w * jointMatrices[j_idx.w];

    float3 vPos = float3(v.p[0], v.p[1], v.p[2]);
    float3 vNormal = float3(v.n[0], v.n[1], v.n[2]);
    float4 vTan = float4(v.t[0], v.t[1], v.t[2], v.t[3]);

    // Transform the position using the full 4x4 skin matrix
    float4 animatedPos = mul(skinMatrix, float4(vPos, 1.0));

    // Normals and tangents use only the 3x3 rotational portion (no translation).
    float3x3 skinRot = float3x3(
        skinMatrix[0].xyz,
        skinMatrix[1].xyz,
        skinMatrix[2].xyz);

    float3 animatedNormal  = normalize(mul(skinRot, vNormal));
    float3 animatedTangent = normalize(mul(skinRot, vTan.xyz));

    // Write the animated data to the output buffer element-by-element
    OutputVertex outVertex;
    outVertex.p[0] = animatedPos.x;
    outVertex.p[1] = animatedPos.y;
    outVertex.p[2] = animatedPos.z;

    outVertex.n[0] = animatedNormal.x;
    outVertex.n[1] = animatedNormal.y;
    outVertex.n[2] = animatedNormal.z;

    outVertex.t[0] = animatedTangent.x;
    outVertex.t[1] = animatedTangent.y;
    outVertex.t[2] = animatedTangent.z;
    outVertex.t[3] = vTan.w; // preserve handedness

    outVertex.uv[0] = v.uv[0];
    outVertex.uv[1] = v.uv[1];

    outputVertices[vertexId] = outVertex;
}
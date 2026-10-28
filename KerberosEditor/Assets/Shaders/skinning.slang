struct InputVertex
{
    float p[3];
    float n[3];
    float t[4];
    float uv[2];
};

struct OutputVertex
{
    float p[3];
    float n[3];
    float t[4];
    float uv[2];
};

// 3x4 affine bone matrix, stored as three explicit rows.
struct BoneMatrix
{
    float4 r0;
    float4 r1;
    float4 r2;
};

[[vk::binding(0, 0)]] StructuredBuffer<InputVertex> inputVertices;
[[vk::binding(1, 0)]] RWStructuredBuffer<OutputVertex> outputVertices;
[[vk::binding(2, 0)]] StructuredBuffer<BoneMatrix> jointMatrices;
[[vk::binding(3, 0)]] StructuredBuffer<uint4> jointIndices;
[[vk::binding(4, 0)]] StructuredBuffer<float4> jointWeights;

struct SkinningPushConstant
{
    uint sourceFirst;
    uint outputFirst;
    uint vertexCount;
    uint outputByteOffset;
    uint firstSkinMatrix;
    uint jointCount;
    uint _pad0;
    uint _pad1;
};
[[vk::push_constant]] SkinningPushConstant pushConstants;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint localVertex = dispatchThreadID.x;
    if (localVertex >= pushConstants.vertexCount)
        return;

    const uint sourceIndex = pushConstants.sourceFirst + localVertex;
    const uint outputIndex = pushConstants.outputFirst + localVertex;

    InputVertex v = inputVertices[sourceIndex];
    uint4 indices = jointIndices[sourceIndex];
    float4 weights = jointWeights[sourceIndex];

    // Defensive normalization; imported weights should already sum to one.
    const float weightSum = weights.x + weights.y + weights.z + weights.w;
    if (weightSum > 1e-6)
        weights /= weightSum;
    else
        weights = float4(1.0, 0.0, 0.0, 0.0);

    const uint maxJoint = pushConstants.jointCount > 0 ? pushConstants.jointCount - 1 : 0;
    const uint base = pushConstants.firstSkinMatrix;

    BoneMatrix b0 = jointMatrices[base + min(indices.x, maxJoint)];
    BoneMatrix b1 = jointMatrices[base + min(indices.y, maxJoint)];
    BoneMatrix b2 = jointMatrices[base + min(indices.z, maxJoint)];
    BoneMatrix b3 = jointMatrices[base + min(indices.w, maxJoint)];

    const float4 r0 = weights.x * b0.r0 + weights.y * b1.r0 + weights.z * b2.r0 + weights.w * b3.r0;
    const float4 r1 = weights.x * b0.r1 + weights.y * b1.r1 + weights.z * b2.r1 + weights.w * b3.r1;
    const float4 r2 = weights.x * b0.r2 + weights.y * b1.r2 + weights.z * b2.r2 + weights.w * b3.r2;

    const float3 position = float3(v.p[0], v.p[1], v.p[2]);
    const float3 normal = float3(v.n[0], v.n[1], v.n[2]);
    const float3 tangent = float3(v.t[0], v.t[1], v.t[2]);
    const float4 position4 = float4(position, 1.0);

    const float3 skinnedPosition = float3(
        dot(r0, position4),
        dot(r1, position4),
        dot(r2, position4));

    const float3 skinnedNormal = normalize(float3(
        dot(r0.xyz, normal),
        dot(r1.xyz, normal),
        dot(r2.xyz, normal)));

    const float3 skinnedTangent = normalize(float3(
        dot(r0.xyz, tangent),
        dot(r1.xyz, tangent),
        dot(r2.xyz, tangent)));

    OutputVertex outVertex;
    outVertex.p[0] = skinnedPosition.x;
    outVertex.p[1] = skinnedPosition.y;
    outVertex.p[2] = skinnedPosition.z;

    outVertex.n[0] = skinnedNormal.x;
    outVertex.n[1] = skinnedNormal.y;
    outVertex.n[2] = skinnedNormal.z;

    outVertex.t[0] = skinnedTangent.x;
    outVertex.t[1] = skinnedTangent.y;
    outVertex.t[2] = skinnedTangent.z;
    outVertex.t[3] = v.t[3]; // preserve handedness

    outVertex.uv[0] = v.uv[0];
    outVertex.uv[1] = v.uv[1];

    outputVertices[outputIndex] = outVertex;
}
